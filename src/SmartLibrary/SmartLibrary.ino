/*
  智能图书馆 ESP32 主程序
  集成：NFC + 条码扫描 + 传感器 + MQTT云端通信

  功能：
  1. NFC 检测图书借还
  2. 条码扫描注册新书（云端爬取信息）
  3. 离线本地缓存
  4. 华为云 MQTT 状态上报
  5. DHT11 湿度检测 + 风扇自动联动

  引脚分配：
  - NFC (HSPI):   SDA=15, SCK=14, MOSI=13, MISO=12, RST=4
  - 条码 (UART2): TX=33, RX=25, 波特率=9600
  - I2C:          SDA=21, SCL=22 (BMP280 + OLED)
  - RGB LED:      R=16, G=17, B=18
  - 光敏:         GPIO 32
  - DHT11:        GPIO 23
  - 风扇电机:     GPIO 26 (PWM)
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Ticker.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <SPIFFS.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <HardwareSerial.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHTesp.h>

// ============================================================
// OLED 配置
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ============================================================
// 全局对象
// ============================================================
WiFiClient          espClient;
PubSubClient        mqtt(espClient);
HardwareSerial      barcodeSerial(2);  // UART2
Adafruit_BMP280     bmp280;
SPIClass            hspi(HSPI);        // 使用 HSPI，避免与 I2C 和 RGB 冲突
MFRC522DriverPinSimple ss_pin(15);
MFRC522DriverSPI    driver{ss_pin, hspi};  // 传入 HSPI
MFRC522             mfrc522{driver};

// ============================================================
// WiFi 配置
// ============================================================
const char* WIFI_SSID     = "iQOO Z9 Turbo";
const char* WIFI_PASSWORD = "cwh979946";

//api

const char* DEEPSEEK_API_URL = "https://api.deepseek.com/v1/chat/completions";
const char* DEEPSEEK_API_KEY = "sk-b6e5c8309b4d45b8ba1572b5b47b11b7"; 


// ============================================================
// 华为云 MQTT 三元组
// ============================================================
#define DEVICE_ID         "69e97a34e094d61592351ea4_pLib"
#define DEVICE_SECRET     "wwdx_wztx121"
const char* MQTT_CLIENT_ID  = "69e97a34e094d61592351ea4_pLib_0_0_2026042307";
const char* MQTT_USER       = "69e97a34e094d61592351ea4_pLib";
const char* MQTT_PASSWORD   = "ad015bf0214723c220b4a1f2b87a92c816864a39532a1c54eb2d63fe55367d4c";
const char* MQTT_SERVER     = "f5644079de.st1.iotda-device.cn-east-3.myhuaweicloud.com";
const int   MQTT_PORT       = 1883;

// ============================================================
// MQTT 主题（华为云 IoTDA 标准主题）
// ============================================================
#define TOPIC_PROP_REPORT    "$oc/devices/" DEVICE_ID "/sys/properties/report"
#define TOPIC_CMD            "$oc/devices/" DEVICE_ID "/sys/commands/#"
#define TOPIC_CMD_RESPONSE   "$oc/devices/" DEVICE_ID "/sys/commands/response/request_id="

// ============================================================
// 引脚定义
// ============================================================
#define I2C_SDA             21
#define I2C_SCL             22
#define RGB_R                16
#define RGB_G                18
#define RGB_B                17
#define PHOTORESISTOR_PIN    32
#define BARCODE_TX_PIN       33   //803DRXD
#define BARCODE_RX_PIN       25   //803DTXD
#define BARCODE_BAUD         9600
#define NFC_RST_PIN          4
#define DHT11_PIN            23
DHTesp              dht;
#define FAN_PIN              19
#define HUMIDITY_THRESHOLD   70  // 湿度超过70%启动风扇

// ============================================================
// 系统状态机
// ============================================================
enum class SystemState {
  IDLE,           // 正常监控，等待 NFC
  AWAITING_SCAN,  // 检测到新书，等待扫码 ISBN
  REGISTERING,   // 正在注册（已有 UID + ISBN）
  MONITORING     // 监控模式（正常借还）
};
SystemState systemState = SystemState::IDLE;

// ============================================================
// 图书记录结构
// ============================================================
struct BookRecord {
  String nfcUID;       // NFC 标签 UID
  String isbn;         // ISBN 条码
  String title;        // 书名
  String author;       // 作者
  String publisher;    // 出版社
  int    publishYear;  // 出版年
  bool   onShelf;      // 是否在架
  unsigned long lastUpdate;
  int    borrowCount;  // 累计借出次数
  unsigned long totalReadingSec;  // 累计阅读时长（秒）
  unsigned long lastBorrowTime;   // 上次借出时的 millis()
};
#define MAX_BOOKS 100
BookRecord bookShelf[MAX_BOOKS];
int bookCount = 0;

// ============================================================
// 全局变量
// ============================================================
float    temperature;
long     lastSensorReport = 0;
int      photoValue;
int      rVal = 255, gVal = 255, bVal = 255;
bool     deviceSwitch = false;
float    humidity = 0;
bool     fanActive = false;
bool     fanManualOverride = false;  // true = 手动开启，风扇常转不受湿度控制

String   barcodeBuffer  = "";
String   pendingNFCUID = "";  // 待注册的 NFC UID
String   lastDetectedUID = "";  // 上次检测到的卡片 UID
unsigned long awaitingStart = 0;  // 进入等待扫码状态的时间


// AI相关变量
bool waitingForAI = false;
unsigned long aiStartTime = 0;
const unsigned long AI_TIMEOUT = 15000;
int aiCallCount = 0;
unsigned long lastAIResetDate = 0;
const int MAX_DAILY_AI_CALLS = 50;
// ============================================================
// 离线图书缓存文件
// ============================================================
const char* BOOK_CACHE_FILE = "/book_cache.json";

// ============================================================
// ============================================================
// 函数声明
// ============================================================
void setupWiFi();
void setupMQTT();
void reconnectMQTT();
void publishSensorData();
void handleCloudCommand(char* topic, byte* payload, unsigned int length);
void sendCommandResponse(const char* requestId, bool success);
void updateFan();

// AI函数
bool sendToDeepSeek(String userMessage, String systemPrompt = "");
void displayAIResponse(String response);
void askAI(String question);
void askBookRecommendation(String currentBookTitle);
bool canCallAI();
void recordAICall();
void checkSerialCommand();

// NFC
void initNFC();
String getUIDString();
void checkNFC();

// 条码
void checkBarcode();

// 图书管理
void initBookStorage();
bool loadBooks();
bool saveBooks();
int  findBookByUID(const String& uid);
int  findBookByISBN(const String& isbn);
void onBookDetected(const String& uid);
void onBookRegistered(const String& uid, const String& isbn);
void reportBookStatus(const String& uid, bool onShelf);

// 显示
void displayStatus(const String& line1, const String& line2 = "", const String& line3 = "");
void displayBook(const BookRecord& book);

// LED 控制
void setRGB(int r, int g, int b);
void ledSuccess();
void ledError();
void ledWaiting();

// MQTT 回调
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  barcodeSerial.begin(BARCODE_BAUD, SERIAL_8N1, BARCODE_RX_PIN, BARCODE_TX_PIN);
  analogReadResolution(10);

  Serial.println();
  Serial.println("========================================");
  Serial.println("  智能图书馆系统 v1.0 启动中...");
  Serial.println("========================================");

  // ----- 引脚初始化 -----
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  pinMode(PHOTORESISTOR_PIN, INPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, HIGH);  // PNP初始关闭（HIGH=三极管截止）
  setRGB(255, 255, 255);  // 白色等待

  // ----- I2C 初始化 -----
  Wire.begin(I2C_SDA, I2C_SCL);

  // ----- OLED 初始化 -----
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED 初始化失败");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.display();
  }

  // ----- BMP280 初始化 -----
  if (!bmp280.begin(0x76)) {
    Serial.println("BMP280 初始化失败");
  }

  // ----- DHT11 初始化 -----
  dht.setup(DHT11_PIN, DHTesp::DHT11);
  Serial.println("DHT11 初始化完成");

  // ----- SPIFFS 初始化 -----
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 初始化失败");
  } else {
    Serial.println("SPIFFS 初始化成功");
    loadBooks();
  }

  // ----- NFC 初始化 -----
  initNFC();

  // ----- WiFi + MQTT -----
  setupWiFi();
  setupMQTT();

  displayStatus("Smart Library", "System Ready", "Scan a book...");
  setRGB(0, 255, 0);  // 绿色 = 就绪
  Serial.println("\n系统就绪，请放置图书...\n");
}

// ============================================================
// loop()
// ============================================================
void loop() {
  // MQTT 状态维护
  if (!mqtt.connected()) {
    reconnectMQTT();
  }
  mqtt.loop();

  // NFC 检测
  checkNFC();

  // 条码检测
  checkBarcode();

  // 超时处理：等待扫码超过 30 秒则返回 IDLE
  if (systemState == SystemState::AWAITING_SCAN) {
    if (millis() - awaitingStart > 30000) {
      Serial.println("[超时] 等待扫码超时，返回监控模式");
      systemState = SystemState::IDLE;
      pendingNFCUID = "";
      setRGB(0, 255, 0);
      displayStatus("Timeout", "Returning to", "monitoring...");
      delay(2000);
      displayStatus("Smart Library", "Monitoring...", "Place a book...");
    }
  }

  // 定时上报传感器数据（每 10 秒）
  long now = millis();
  if (now - lastSensorReport > 10000) {
    lastSensorReport = now;
    publishSensorData();
  }

  // AI 超时检测
  if (waitingForAI && (millis() - aiStartTime > AI_TIMEOUT)) {
    waitingForAI = false;
    Serial.println("[AI] 请求超时");
    setRGB(0, 255, 0);
  }

  // 串口命令输入处理（调试/测试用）
  checkSerialCommand();

  delay(50);
}

// ============================================================
// 串口命令处理（调试/测试AI功能）
// ============================================================
void checkSerialCommand() {
  static String inputBuffer = "";
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        Serial.println("[串口] 收到命令: " + inputBuffer);
        askAI(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}

// ============================================================
// WiFi 连接
// ============================================================
void setupWiFi() {
  Serial.printf("连接 WiFi: %s\n", WIFI_SSID);
  displayStatus("Connecting", WIFI_SSID, "...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi 已连接");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
}

// ============================================================
// MQTT 初始化
// ============================================================
void setupMQTT() {
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  reconnectMQTT();
}

// ============================================================
// MQTT 重连
// ============================================================
void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.println("连接 MQTT 服务器...");
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("MQTT 连接成功");
      mqtt.subscribe(TOPIC_CMD);
    } else {
      Serial.printf("MQTT 连接失败: %d，3秒后重试...\n", mqtt.state());
      delay(3000);
    }
  }
}

// ============================================================
// MQTT 回调
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  String t   = String(topic);

  Serial.println("\n>>> MQTT 收到消息");
  Serial.print("Topic: ");
  Serial.println(t);
  Serial.print("Payload: ");
  Serial.println(msg);

  // ---- 命令下发（包含 LED 控制和书籍信息下发）----
  if (t.indexOf("commands") != -1) {
    handleCloudCommand(topic, payload, length);
  }
}

// ============================================================
// 云端命令处理（RGB 控制 + 书籍信息下发）
// ============================================================
void handleCloudCommand(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return;

  String cmdName = doc["command_name"] | "";

  if (cmdName == "LEDControl") {
    rVal = doc["paras"]["RED"]   | 0;
    gVal = doc["paras"]["GREEN"] | 0;
    bVal = doc["paras"]["BLUE"]  | 0;
    deviceSwitch = doc["paras"]["Switch"] | false;

    if (deviceSwitch) {
      analogWrite(RGB_R, 255 - rVal);
      analogWrite(RGB_G, 255 - gVal);
      analogWrite(RGB_B, 255 - bVal);
    } else {
      setRGB(255, 255, 255);
    }
  }
  else if (cmdName == "FanControl") {
    String fanCmd = doc["paras"]["fan"] | "off";
    if (fanCmd == "on") {
      fanManualOverride = true;
      digitalWrite(FAN_PIN, LOW);  // PNP 低电平导通
      fanActive = true;
      Serial.println("[命令] 风扇强制开启");
    } else {
      fanManualOverride = false;
      digitalWrite(FAN_PIN, HIGH);  // PNP 高电平关闭
      fanActive = false;
      Serial.println("[命令] 风扇控制交还湿度自动");
    }
  }
  else if (cmdName == "deliver_book_info") {
    // 云端爬取书籍信息后下发的命令
    String isbn      = doc["paras"]["isbn"]      | "";
    String title     = doc["paras"]["title"]     | "";
    String author    = doc["paras"]["author"]    | "";
    String publisher = doc["paras"]["publisher"] | "";
    int    year      = doc["paras"]["year"]      | 0;

    int idx = findBookByISBN(isbn);
    if (idx >= 0) {
      bookShelf[idx].title       = title;
      bookShelf[idx].author      = author;
      bookShelf[idx].publisher   = publisher;
      bookShelf[idx].publishYear = year;
      bookShelf[idx].lastUpdate  = millis();
      saveBooks();
      Serial.println(">>> 已更新本地书籍信息（来自云端）");
      displayStatus("Updated:", title.substring(0, 16), author.substring(0, 16));
      ledSuccess();
    } else {
      Serial.println(">>> 收到未知 ISBN 的书籍信息，忽略");
    }
  }

  // 提取 request_id 发送响应
  String topicStr = String(topic);
  int eqIdx = topicStr.indexOf("request_id=");
  String requestId = "";
  if (eqIdx >= 0) {
    requestId = topicStr.substring(eqIdx + 11);
  }
  sendCommandResponse(requestId.c_str(), true);
}

// ============================================================
// 发送命令响应
// ============================================================
void sendCommandResponse(const char* requestId, bool success) {
  char topic[256];
  char body[128];
  sprintf(topic, "%s%s", TOPIC_CMD_RESPONSE, requestId);
  sprintf(body, "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{\"result\":\"%s\"}}",
          success ? "success" : "failed");
  mqtt.publish(topic, body);
}

// ============================================================
// 定时上报传感器数据
// ============================================================
void publishSensorData() {
  temperature  = bmp280.readTemperature();
  photoValue  = analogRead(PHOTORESISTOR_PIN);
  long runningSec = millis() / 1000;

  // 读取 DHT11 湿度
  TempAndHumidity th = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE) {
    humidity = th.humidity;
  } else {
    humidity = 0;
    Serial.printf("DHT11 读取失败: %s\n", dht.getStatusString());
  }

  // 风扇联动
  updateFan();

  Serial.printf("传感器: T=%.1f H=%.0f%% P=%d Fan=%d\n", temperature, humidity, photoValue, fanActive ? 1 : 0);

  // 计算在架/借出数量
  int onShelfCount = 0;
  int borrowedCount = 0;
  for (int i = 0; i < bookCount; i++) {
    if (bookShelf[i].onShelf) onShelfCount++;
    else borrowedCount++;
  }

  char props[512];
  sprintf(props,
          "\"Temperature\":%.1f,\"Humidity\":%.0f,\"Photores\":%d,\"RED\":%d,\"GREEN\":%d,\"BLUE\":%d,"
          "\"Switch\":%d,\"BookCount\":%d,\"OnShelfCount\":%d,\"BorrowedCount\":%d,\"Time\":%ld}}]}",
          temperature, humidity, photoValue, rVal, gVal, bVal, deviceSwitch ? 1 : 0,
          bookCount, onShelfCount, borrowedCount, runningSec);

  char msg[512];
  sprintf(msg, "{\"services\":[{\"service_id\":\"Env\",\"properties\":{%s", props);

  if (mqtt.publish(TOPIC_PROP_REPORT, msg)) {
    Serial.println("MQTT 上报成功");
  } else {
    Serial.println("MQTT 上报失败!");
  }

  // OLED 显示
  char tStr[16], pStr[16], hStr[16];
  dtostrf(temperature, 4, 1, tStr);
  sprintf(pStr, "%d LT", photoValue);
  sprintf(hStr, "%.0f%%", humidity);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.printf("T:%s H:%.0f%%", tStr, humidity);
  display.setCursor(0, 16);
  display.printf("L:%d B:%d F:%s", photoValue, bookCount, fanActive ? "ON" : "OFF");
  display.display();
}

// ============================================================
// 风扇联动：湿度超过阈值自动启动
// ============================================================
void updateFan() {
  if (fanManualOverride) return;  // 手动开启时跳过自动逻辑

  if (humidity > HUMIDITY_THRESHOLD) {
    if (!fanActive) {
      fanActive = true;
      digitalWrite(FAN_PIN, LOW);  // LOW → 三极管导通 → 风扇启动
      Serial.printf("湿度 %.0f%% > %d%%，风扇启动\n", humidity, HUMIDITY_THRESHOLD);
    }
  } else {
    if (fanActive) {
      fanActive = false;
      digitalWrite(FAN_PIN, HIGH);  // HIGH → 三极管关断 → 风扇停止
      Serial.printf("湿度 %.0f%% <= %d%%，风扇停止\n", humidity, HUMIDITY_THRESHOLD);
    }
  }
}

// ============================================================
// NFC 初始化
// ============================================================
void initNFC() {
  hspi.begin();  // HSPI: SCK=14, MISO=12, MOSI=13, SS=15
  mfrc522.PCD_Init();
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);
  Serial.println("NFC 模块初始化完成");
}

// ============================================================
// 获取卡片 UID 字符串
// ============================================================
String getUIDString() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ============================================================
// NFC 检测主逻辑
// ============================================================
void checkNFC() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return; 
  }

  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = getUIDString();
  Serial.println("\n========== NFC 检测 ==========");
  Serial.print("卡片 UID: ");
  Serial.println(uid);
  Serial.print("当前状态: ");
  Serial.println((int)systemState);

  mfrc522.PICC_HaltA();

  // 忽略同一张卡重复快速检测（50ms 间隔内）
  static unsigned long lastTapMs = 0;
  if (uid == lastDetectedUID) {
    if (millis() - lastTapMs < 200) {
      Serial.println(">>> 重复检测（间隔太短），忽略");
      return;
    }
  }
  lastTapMs = millis();

  // 记录新卡片
  lastDetectedUID = uid;

  switch (systemState) {
    case SystemState::IDLE:
    case SystemState::MONITORING:
      onBookDetected(uid);
      break;
    case SystemState::AWAITING_SCAN:
      pendingNFCUID = uid;
      awaitingStart = millis();
      displayStatus("New Book Detected", "Scan ISBN", uid.substring(0, 8));
      ledWaiting();
      break;
    default:
      break;
  }
}

// ============================================================
// 条码扫描检测
// ============================================================
void checkBarcode() {
  static int dbg_count = 0;
  int avail = barcodeSerial.available();
  if (avail > 0) {
    dbg_count++;
    if (dbg_count <= 3 || avail > 10) {
      Serial.printf("[条码] avail=%d (total=%d)\n", avail, dbg_count);
    }
  }
  while (barcodeSerial.available() > 0) {
    char c = barcodeSerial.read();
    if (c == '\n' || c == '\r') {
      if (barcodeBuffer.length() > 0) {
        barcodeBuffer.trim();
        Serial.println("\n========== 条码扫描 ==========");
        Serial.print("ISBN: ");
        Serial.println(barcodeBuffer);

        if (systemState == SystemState::AWAITING_SCAN && pendingNFCUID != "") {
          // 检查卡片是否仍在（防用户中途取走书）
          if (lastDetectedUID != pendingNFCUID) {
            Serial.println(">>> 图书已被取走，取消注册");
            displayStatus("Cancelled:", "Book removed", "before scan");
            ledError();
            systemState = SystemState::IDLE;
            pendingNFCUID = "";
            barcodeBuffer = "";
            return;
          }
          // 匹配：完成注册
          onBookRegistered(pendingNFCUID, barcodeBuffer);
          barcodeBuffer = "";
          return;
        } else {
          // 独立扫码：查询本地
          int idx = findBookByISBN(barcodeBuffer);
          if (idx >= 0) {
            Serial.println(">>> 查到图书:");
            displayBook(bookShelf[idx]);
          } else {
            Serial.println(">>> 未注册的 ISBN，请先进行注册");
            displayStatus("Unknown ISBN", barcodeBuffer, "Not registered");
            ledError();
          }
        }
        barcodeBuffer = "";
      }
    } else {
      barcodeBuffer += c;
    }
  }
}

// ============================================================
// 图书被检测
// ============================================================
void onBookDetected(const String& uid) {
  int idx = findBookByUID(uid);
  if (idx >= 0) {
    // 已注册的书 → 更改在架状态
    bool wasOnShelf = bookShelf[idx].onShelf;
    bookShelf[idx].onShelf = !wasOnShelf;
    bookShelf[idx].lastUpdate = millis();
    saveBooks();

    if (wasOnShelf) {
      // 在架 → 借出
      Serial.println(">>> 图书已被借出（再次检测，翻转）");
      bookShelf[idx].lastBorrowTime = millis();
      bookShelf[idx].borrowCount++;
      displayStatus("Borrowed", "");
      reportBookStatus(uid, false);
      ledError();
    } else {
      // 不在架 → 归还
      Serial.println(">>> 图书已归还书架（再次检测，翻转）");
      if (bookShelf[idx].lastBorrowTime > 0) {
        unsigned long readingSec = (millis() - bookShelf[idx].lastBorrowTime) / 1000;
        bookShelf[idx].totalReadingSec += readingSec;
        Serial.printf("    本次阅读: %lu 秒, 累计: %lu 秒\n", readingSec, bookShelf[idx].totalReadingSec);
      }
      bookShelf[idx].lastBorrowTime = 0;
      displayStatus("Returned", "");
      reportBookStatus(uid, true);
      ledSuccess();
      // 归还成功后，调用 AI 推荐类似书籍
      if (bookShelf[idx].title.length() > 0) {
        askBookRecommendation(bookShelf[idx].title);
      }
    }
  } else {
    // 未注册图书 → 进入等待扫码注册流程
    Serial.println(">>> 未注册的图书，进入注册流程...");
    pendingNFCUID = uid;
    awaitingStart = millis();
    systemState = SystemState::AWAITING_SCAN;
    displayStatus("New Book!", "Scan ISBN", "to register");
    ledWaiting();
  }
}



// ============================================================
// 注册新图书
// ============================================================
void onBookRegistered(const String& uid, const String& isbn) {
  if (bookCount >= MAX_BOOKS) {
    Serial.println(">>> 书架已满，无法注册更多图书");
    displayStatus("Error:", "Book shelf", "is full!");
    ledError();
    systemState = SystemState::IDLE;
    pendingNFCUID = "";
    return;
  }

  // 查找是否已有记录（仅有 UID）
  int idx = findBookByUID(uid);

  if (idx < 0) {
    // 新建记录
    idx = bookCount;
    bookCount++;
    bookShelf[idx].nfcUID = uid;
  }

  bookShelf[idx].isbn       = isbn;
  bookShelf[idx].onShelf     = true;
  bookShelf[idx].lastUpdate = millis();

  Serial.println(">>> 图书注册成功");
  Serial.print("    UID:  ");
  Serial.println(uid);
  Serial.print("    ISBN: ");
  Serial.println(isbn);

  saveBooks();
  displayStatus("Registered:", isbn, uid.substring(0, 8));
  ledSuccess();

  // 通过属性上报通知云端 → 触发云端爬取流程
  char regMsg[512];
  sprintf(regMsg,
          "{\"services\":[{\"service_id\":\"Env\",\"properties\":{"
          "\"book_uid\":\"%s\",\"book_isbn\":\"%s\",\"book_status\":\"registered\"}}]}",
          uid.c_str(), isbn.c_str());
  Serial.print(">>> 发送内容: ");
  Serial.println(regMsg);
  if (mqtt.publish(TOPIC_PROP_REPORT, regMsg)) {
    Serial.println(">>> 图书注册 MQTT 上报成功");
  } else {
    Serial.println(">>> 图书注册 MQTT 上报失败!");
  }

  systemState = SystemState::IDLE;
  pendingNFCUID = "";
}

// ============================================================
// 上报图书状态到云端
// ============================================================
void reportBookStatus(const String& uid, bool onShelf) {
  int idx = findBookByUID(uid);
  const char* status = onShelf ? "returned" : "borrowed";

  // 计算在架/借出数
  int onShelfCnt = 0, borrowedCnt = 0;
  for (int i = 0; i < bookCount; i++) {
    if (bookShelf[i].onShelf) onShelfCnt++;
    else borrowedCnt++;
  }

  // 每本书的统计数据
  int borrowCnt = (idx >= 0) ? bookShelf[idx].borrowCount : 0;
  unsigned long readingSec = (idx >= 0) ? bookShelf[idx].totalReadingSec : 0;
  const char* bookTitle = (idx >= 0 && bookShelf[idx].title.length() > 0)
                          ? bookShelf[idx].title.c_str() : "";

  char statusMsg[1024];
  sprintf(statusMsg,
          "{\"services\":[{\"service_id\":\"Env\",\"properties\":{"
          "\"book_uid\":\"%s\",\"book_isbn\":\"%s\",\"book_title\":\"%s\","
          "\"book_status\":\"%s\","
          "\"book_borrow_count\":%d,\"book_reading_sec\":%lu}}]}",
          uid.c_str(),
          idx >= 0 ? bookShelf[idx].isbn.c_str() : "",
          bookTitle,
          status,
          borrowCnt, readingSec);
  Serial.print(">>> 状态上报内容: ");
  Serial.println(statusMsg);
  if (mqtt.publish(TOPIC_PROP_REPORT, statusMsg)) {
    Serial.println(">>> 状态上报 MQTT 成功");
  } else {
    Serial.println(">>> 状态上报 MQTT 失败!");
  }
}

// ============================================================
// 查找图书（按 NFC UID）
// ============================================================
int findBookByUID(const String& uid) {
  for (int i = 0; i < bookCount; i++) {
    if (bookShelf[i].nfcUID.equalsIgnoreCase(uid)) return i;
  }
  return -1;
}

// ============================================================
// 查找图书（按 ISBN）
// ============================================================
int findBookByISBN(const String& isbn) {
  for (int i = 0; i < bookCount; i++) {
    if (bookShelf[i].isbn.equalsIgnoreCase(isbn)) return i;
  }
  return -1;
}

// ============================================================
// 加载本地图书缓存
// ============================================================
bool loadBooks() {
  if (!SPIFFS.exists(BOOK_CACHE_FILE)) {
    File f = SPIFFS.open(BOOK_CACHE_FILE, FILE_WRITE);
    if (f) { f.print("{\"books\":[],\"count\":0}"); f.close(); }
    Serial.println("已创建空图书缓存");
    return false;
  }

  File f = SPIFFS.open(BOOK_CACHE_FILE, FILE_READ);
  if (!f) return false;

  String content;
  while (f.available()) content += (char)f.read();
  f.close();

  StaticJsonDocument<8192> doc;
  DeserializationError err = deserializeJson(doc, content);
  if (err) {
    Serial.println("图书缓存解析失败，将重建");
    return false;
  }

  bookCount = doc["count"] | 0;
  JsonArray books = doc["books"].as<JsonArray>();
  int i = 0;
  for (JsonObject b : books) {
    if (i >= MAX_BOOKS) break;
    bookShelf[i].nfcUID     = b["nfc_uid"]    | "";
    bookShelf[i].isbn       = b["isbn"]        | "";
    bookShelf[i].title      = b["title"]       | "";
    bookShelf[i].author     = b["author"]      | "";
    bookShelf[i].publisher  = b["publisher"]  | "";
    bookShelf[i].publishYear= b["publishYear"] | 0;
    bookShelf[i].onShelf    = b["onShelf"]     | true;
    bookShelf[i].lastUpdate = b["lastUpdate"]  | 0;
    bookShelf[i].borrowCount = b["borrowCount"] | 0;
    bookShelf[i].totalReadingSec = b["totalReadingSec"] | 0;
    bookShelf[i].lastBorrowTime = b["lastBorrowTime"] | 0;
    i++;
  }
  bookCount = i;
  Serial.printf("已加载 %d 本图书\n", bookCount);
  return true;
}

// ============================================================
// 保存图书到本地缓存
// ============================================================
bool saveBooks() {
  StaticJsonDocument<8192> doc;
  doc["count"] = bookCount;
  JsonArray books = doc["books"].to<JsonArray>();
  for (int i = 0; i < bookCount; i++) {
    JsonObject b = books.add<JsonObject>();
    b["nfc_uid"]     = bookShelf[i].nfcUID;
    b["isbn"]        = bookShelf[i].isbn;
    b["title"]       = bookShelf[i].title;
    b["author"]      = bookShelf[i].author;
    b["publisher"]   = bookShelf[i].publisher;
    b["publishYear"] = bookShelf[i].publishYear;
    b["onShelf"]     = bookShelf[i].onShelf;
    b["lastUpdate"]  = bookShelf[i].lastUpdate;
    b["borrowCount"] = bookShelf[i].borrowCount;
    b["totalReadingSec"] = bookShelf[i].totalReadingSec;
    b["lastBorrowTime"]  = bookShelf[i].lastBorrowTime;
  }

  File f = SPIFFS.open(BOOK_CACHE_FILE, FILE_WRITE);
  if (!f) return false;
  size_t written = serializeJson(doc, f);
  f.close();
  Serial.printf("图书缓存已保存 (%d bytes)\n", written);
  return written > 0;
}

// ============================================================
// OLED 
// ============================================================
void displayStatus(const String& line1, const String& line2, const String& line3) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  // 借还状态居中显示
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(line1.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 20);
  display.println(line1);
  display.display();
}

// ============================================================
// OLED 显示图书信息
// ============================================================
void displayBook(const BookRecord& book) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("=== Book Info ===");
  display.setCursor(0, 12);
  display.println(book.title.substring(0, 21));
  display.setCursor(0, 24);
  display.println(book.author.substring(0, 21));
  display.setCursor(0, 36);
  display.printf("ISBN: %s", book.isbn.c_str());
  display.setCursor(0, 48);
  display.printf("Status: %s", book.onShelf ? "On Shelf" : "Borrowed");
  display.display();
}

// ============================================================
// LED 控制
// ============================================================
void setRGB(int r, int g, int b) {
  analogWrite(RGB_R, 255 - r);
  analogWrite(RGB_G, 255 - g);
  analogWrite(RGB_B, 255 - b);
}

void ledSuccess() {
  setRGB(0, 0, 255);  // 蓝色 = 还书
  delay(1000);
  if (systemState == SystemState::IDLE) setRGB(0, 255, 0);
}

void ledError() {
  setRGB(255, 0, 0);  // 红色 = 借出
  delay(1000);
  if (systemState == SystemState::IDLE) setRGB(0, 255, 0);
}

void ledWaiting() {
  setRGB(255, 255, 0);  // 黄色闪烁
}


// ============================================================
// 本地AI功能实现（aborted）
// ============================================================

bool canCallAI() {
  if (millis() - lastAIResetDate > 86400000) {
    aiCallCount = 0;
    lastAIResetDate = millis();
  }
  return aiCallCount < MAX_DAILY_AI_CALLS;
}

void recordAICall() {
  aiCallCount++;
}

bool sendToDeepSeek(String userMessage, String systemPrompt) {
  if (!canCallAI()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (String(DEEPSEEK_API_KEY).length() < 10) return false;
  
  waitingForAI = true;
  aiStartTime = millis();
  
  WiFiClientSecure client;
  HTTPClient https;
  client.setInsecure();
  
  https.begin(client, DEEPSEEK_API_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + DEEPSEEK_API_KEY);
  
  StaticJsonDocument<2048> doc;
  doc["model"] = "deepseek-chat";
  doc["temperature"] = 0.7;
  doc["max_tokens"] = 500;
  
  JsonArray messages = doc["messages"].to<JsonArray>();
  
  if (systemPrompt.length() == 0) {
    systemPrompt = "你是图书馆助手，回答极简，50字以内。";
  }
  JsonObject systemMsg = messages.add<JsonObject>();
  systemMsg["role"] = "system";
  systemMsg["content"] = systemPrompt;
  
  JsonObject userMsg = messages.add<JsonObject>();
  userMsg["role"] = "user";
  userMsg["content"] = userMessage;
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  int httpCode = https.POST(requestBody);
  bool success = false;

  Serial.printf("[AI] HTTP响应码: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    String response = https.getString();
    Serial.printf("[AI] 原始响应长度: %d bytes\n", response.length());
    Serial.println("[AI] 原始响应内容: " + response);

    StaticJsonDocument<8192> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (!error) {
      String aiReply = responseDoc["choices"][0]["message"]["content"] | "";
      Serial.println("[AI] 解析后回复: " + aiReply);
      displayAIResponse(aiReply);
      recordAICall();
      success = true;
    } else {
      Serial.println("[AI] JSON解析失败: " + String(error.c_str()));
    }
  } else {
    Serial.printf("[AI] HTTP错误: %d\n", httpCode);
    String errorBody = https.getString();
    Serial.println("[AI] 错误内容: " + errorBody);
  }

  https.end();
  waitingForAI = false;
  return success;
}

void displayAIResponse(String response) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("AI:");
  
  int startPos = 0;
  for (int i = 0; i < 3 && startPos < response.length(); i++) {
    String line = response.substring(startPos, startPos + 20);
    display.setCursor(0, 14 + i * 12);
    display.println(line);
    startPos += 20;
  }
  display.display();
  delay(5000);
  
  // 恢复显示
  displayStatus("Smart Library", "Monitoring...", "Scan a book...");
}

void askAI(String question) {
  if (waitingForAI) return;
  Serial.println("[AI] 问题: " + question);
  displayStatus("Asking AI...", question.substring(0, 16), "");
  ledWaiting();
  sendToDeepSeek(question, "");
  if (systemState == SystemState::IDLE) setRGB(0, 255, 0);
}

void askBookRecommendation(String currentBookTitle) {
  if (currentBookTitle.length() == 0) return;
  String question = "推荐2本类似《" + currentBookTitle + "》的书，每本10字内理由";
  sendToDeepSeek(question, "你是图书推荐专家，回答极简。");
}
