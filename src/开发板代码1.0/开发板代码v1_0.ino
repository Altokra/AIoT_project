// ESP32接入华为云物联网平台完成属性定时上报
// 外设包括：三色灯、BMP280、光敏电阻
// 可在微信小程序中作为本地设备使用

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Ticker.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "HardwareSerial.h"
#include <WiFi.h>
#include <PubSubClient.h>  // 建议使用PubSubClient2.7.0，最新的库不太好用
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>  //显示屏

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// 全局对象定义
WiFiClient espClient;
PubSubClient client(espClient);
HardwareSerial mySerial(1);  // 串口1
Adafruit_BMP280 bmp280;

// WiFi连接参数
const char* ssid     = "iQOO Z9 Turbo";       // WiFi名称
const char* password = "cwh979946";        // WiFi密码

// 华为云设备信息（三元组）
// 三元组生成链接：https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/
#define DEVICE_ID        "69ae7d56e094d615922474f7_exp2"
#define DEVICE_SECRET    "wwdx_wztx121"
const char* CLIENT_ID     = "69ae7d56e094d615922474f7_exp2_0_0_2026031608";
const char* MQTT_USER     = "69ae7d56e094d615922474f7_exp2";
const char* MQTT_PASSWORD = "cd862327a5d809ca3ae376735750895d9feec49a8d7b06ee19c760a7c92e47d7";
const char* MQTT_SERVER   = "f5644079de.st1.iotda-device.cn-east-3.myhuaweicloud.com";  // 华为云MQTT服务器地址
const int   MQTT_PORT     = 1883;                                                 // 华为云MQTT端口

// MQTT主题和消息格式定义
#define SERVICE_ID        "\"Arduino\""         //注意自己的service id是否跟我一样为Arduino，如果不是，请把此处修改为正确的。
#define MQTT_BODY_FORMAT  "{\"services\":[{\"service_id\":" SERVICE_ID ",\"properties\":{%s"
#define MQTT_TOPIC_REPORT      "$oc/devices/" DEVICE_ID "/sys/properties/report"           // 属性上报主题
#define MQTT_TOPIC_GET         "$oc/devices/" DEVICE_ID "/sys/messages/down"               // 下行消息主题
#define MQTT_TOPIC_COMMANDS    "$oc/devices/" DEVICE_ID "/sys/commands/"                   // 命令下发主题
#define MQTT_TOPIC_CMD_RESPONSE "$oc/devices/" DEVICE_ID "/sys/commands/response/request_id="  // 命令响应主题
#define RESPONSE_DATA     "{\"result_code\": 0,\"response_name\": \"COMMAND_RESPONSE\",\"paras\": {\"result\": \"success\"}}"

// 引脚定义
#define BMP_SDA           21    // BMP280 SDA引脚
#define BMP_SCL           22    // BMP280 SCL引脚
#define RED_LED           16    // 红色LED引脚
#define GREEN_LED         18    // 绿色LED引脚
#define BLUE_LED          17    // 蓝色LED引脚
#define PHOTORESISTOR_PIN 32    // 光敏电阻引脚

// 全局变量
float temperature;             // 温度值
long lastReportTime = 0;       // 上次上报时间
int photoResistorValue;        // 光敏电阻值
int redValue, greenValue, blueValue;  // RGB灯颜色值
bool deviceSwitch;             // 设备开关状态
long Time;                    // 时间戳，表示设备运行了多少秒

// 函数声明
void setupWiFi();
void setupMQTT();
void reconnectMQTT();
void publishSensorData(long currentTime);
void callback(char* topic, byte* payload, unsigned int length);
void sendCommandResponse(char* topic);
void initializePins();
bool initializeBMP280();

void setup() {
  // 初始化串口
  Serial.begin(115200);
  mySerial.begin(115200, SERIAL_8N1, 19, 23);  // 串口1引脚映射: RXD:GPIO19, TXD:GPIO23
  analogReadResolution(10);
  
  Serial.println("ESP32 华为云物联网平台连接示例");
  
  // 初始化硬件
  initializePins();
  if (!initializeBMP280()) {
    Serial.println("BMP280初始化失败，程序可能无法正常工作");
  }
  
  // 初始化显示屏
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  display.clearDisplay();
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 35);
  display.println("Initializing OLED");
  display.display();
  delay(250);
  display.clearDisplay();
  
  // 连接WiFi
  setupWiFi();
  
  // 初始化MQTT
  setupMQTT();
}

void loop() {
  // 检查MQTT连接，断开则重连
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // 定时上报传感器数据 (每5秒)
  long currentTime = millis();
  if (currentTime - lastReportTime > 5000) {
    lastReportTime = currentTime;
    publishSensorData(currentTime);
  }
}

/**
 * 初始化引脚模式
 */
void initializePins() {
  // 配置LED引脚为输出模式
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  
  // 初始化LED为关闭状态
  analogWrite(RED_LED, 255);
  analogWrite(GREEN_LED, 255);
  analogWrite(BLUE_LED, 255);
  
  // 配置光敏电阻引脚为输入模式
  pinMode(PHOTORESISTOR_PIN, INPUT);
}

/**
 * 初始化BMP280传感器
 * @return 初始化成功返回true，失败返回false
 */
bool initializeBMP280() {
  Serial.println("初始化BMP280传感器...");
  Wire.begin(BMP_SDA, BMP_SCL);  // 初始化I2C
  return bmp280.begin(0x76);     // 器件的IC地址是0X76或0X77
}

/**
 * 连接WiFi网络
 */
void setupWiFi() {
  Serial.printf("连接到WiFi: %s ...\n", ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi连接成功");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());

  // 配置NTP时间同步 (GMT+8 中国时区)
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("NTP时间同步已配置");
}

/**
 * 初始化MQTT客户端
 */
void setupMQTT() {
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  
  // 订阅必要的主题
  client.subscribe(MQTT_TOPIC_COMMANDS);
  client.subscribe(MQTT_TOPIC_GET);
}

/**
 * 重连MQTT服务器
 */
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.println("尝试连接到MQTT服务器...");
    
    if (client.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("MQTT连接成功");
      
      // 重新订阅主题
      client.subscribe(MQTT_TOPIC_COMMANDS);
      client.subscribe(MQTT_TOPIC_GET);
    } else {
      Serial.printf("MQTT连接失败，错误代码: %d，3秒后重试...\n", client.state());
      delay(3000);
    }
  }
}

/**
 * 上报传感器数据到华为云平台
 */
void publishSensorData(long currentTime) {
  // 读取传感器数据
  photoResistorValue = analogRead(PHOTORESISTOR_PIN);
  temperature = bmp280.readTemperature();
  Time = currentTime / 1000;  // 计算时间计数器

  // 显示屏显示
  display.clearDisplay();

  // 显示日期 (字号1)
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char dateStr[20];
  strftime(dateStr, sizeof(dateStr), "%m-%d %H:%M", &timeinfo);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(dateStr);

  // 显示温度和光敏 (字号2)
  display.setTextSize(2);
  display.setCursor(25, 18);
  display.print(temperature);
  display.setCursor(95, 18);
  display.print("C");
  display.setTextSize(2);
  display.setCursor(25, 44);
  display.print(photoResistorValue);
  display.setCursor(95, 44);
  display.print("LT");
  display.display();
  
  // 打印传感器数据到串口
  Serial.printf("光敏电阻值: %d\n", photoResistorValue);
  Serial.printf("温度: %.2f *C\n", temperature);
  
  // 构建JSON消息
  char properties[256];
  char jsonBuf[512];
  
  sprintf(properties,
          "\"Temperature\":%.2f,\"Photores\":%d,\"RED\":%d,\"GREEN\":%d,\"BLUE\":%d,\"Switch\":%d,\"Time\":%ld}}]}",
          temperature, photoResistorValue, redValue, greenValue, blueValue, deviceSwitch, Time);
  
  sprintf(jsonBuf, MQTT_BODY_FORMAT, properties);
  
  // 发布消息
  client.publish(MQTT_TOPIC_REPORT, jsonBuf);
  Serial.println("上报数据:");
  Serial.println(jsonBuf);
  Serial.println("MQTT数据上报成功");
}

/**
 * MQTT消息回调函数，处理平台下发的命令
 */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n收到平台下发消息:");
  Serial.print("主题: ");
  Serial.println(topic);
  
  // 为payload添加结束符
  payload[length] = '\0';
  Serial.print("内容: ");
  Serial.println((char*)payload);
  
  // 处理命令下发
  if (strstr(topic, MQTT_TOPIC_COMMANDS)) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.println("JSON解析失败");
      return;
    }
    
    // 提取RGB颜色值和开关状态
    redValue = doc["paras"]["RED"];
    greenValue = doc["paras"]["GREEN"];
    blueValue = doc["paras"]["BLUE"];
    deviceSwitch = doc["paras"]["Switch"];
    
    // 打印接收到的参数
    Serial.println("解析到的参数:");
    Serial.printf("RED: %d, GREEN: %d, BLUE: %d, Switch: %d\n", 
                  redValue, greenValue, blueValue, deviceSwitch);
    
    // 控制LED
    if (deviceSwitch) {
      analogWrite(RED_LED, 255 - redValue);
      analogWrite(GREEN_LED, 255 - greenValue);
      analogWrite(BLUE_LED, 255 - blueValue);
    } else {
      // 关闭LED
      analogWrite(RED_LED, 255);
      analogWrite(GREEN_LED, 255);
      analogWrite(BLUE_LED, 255);
    }
    
    // 提取request_id用于响应
    char requestId[100] = {0};
    char* pstr = topic;
    char* p = requestId;
    int flag = 0;
    
    while (*pstr) {
      if (flag) *p++ = *pstr;
      if (*pstr == '=') flag = 1;
      pstr++;
    }
    *p = '\0';
    
    Serial.print("Request ID: ");
    Serial.println(requestId);
    
    // 构建响应主题并发送响应
    char responseTopic[200] = {0};
    strcat(responseTopic, MQTT_TOPIC_CMD_RESPONSE);
    strcat(responseTopic, requestId);
    
    sendCommandResponse(responseTopic);
  }
  // 处理其他下行消息
  else if (strstr(topic, MQTT_TOPIC_GET)) {
    Serial.println("收到下行消息，未处理");
  }
}

/**
 * 发送命令响应到平台
 */
void sendCommandResponse(char* topic) {
  if (client.publish(topic, RESPONSE_DATA)) {
    Serial.println("命令响应发送成功");
  } else {
    Serial.println("命令响应发送失败");
  }
  Serial.println("------------------------");
}
