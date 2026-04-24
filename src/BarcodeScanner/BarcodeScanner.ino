/*
  ESP32 条码扫描 + 书籍查询 集成程序
  功能：
  1. 通过 UART2 接收条码扫描仪数据
  2. 查询本地缓存
  3. 本地未找到时联网查询并自动缓存

  条码扫描仪接线（TTL）：
  - 扫描仪 TX  →  ESP32 GPIO 12 (UART2 RX)
  - 扫描仪 RX  →  ESP32 GPIO 4 (UART2 TX)
  - 扫描仪 VCC →  5V
  - 扫描仪 GND →  GND
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <HardwareSerial.h>

// ============== WiFi 配置 ==============
const char* ssid     = "iQOO Z9 Turbo";       // WiFi名称
const char* password = "cwh979946";        // WiFi密码

// ============== UART 配置 ==============
HardwareSerial barcodeSerial(2);
const int BARCODE_RX_PIN = 12;
const int BARCODE_TX_PIN = 4;
const int BARCODE_BAUD   = 9600;

// ============== 缓存配置 ==============
const char* CACHE_FILE = "/book_cache.json";

// ============== 书籍信息结构 ==============
struct BookInfo {
  String isbn;
  String title;
  String author;
  String publisher;
  int publishYear;
};

// ============== 全局变量 ==============
WiFiClient client;              // HTTP 客户端
String barcodeData = "";        // 条码缓冲区

// ============== 函数声明 ==============
void setupWiFi();
bool loadCache();
bool saveBookToCache(const BookInfo& book);
BookInfo lookupBookLocal(const String& isbn);
BookInfo lookupBookOnline(const String& isbn);
void printBookInfo(const BookInfo& book);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("========================================");
  Serial.println("  ESP32 条码扫描 + 书籍查询系统");
  Serial.println("========================================");

  // 初始化条码扫描仪通信
  barcodeSerial.begin(BARCODE_BAUD, SERIAL_8N1, BARCODE_RX_PIN, BARCODE_TX_PIN);

  // 初始化 SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 初始化失败!");
  } else {
    Serial.println("SPIFFS 初始化成功");
  }

  // 加载本地缓存
  loadCache();

  // 连接 WiFi
  setupWiFi();

  Serial.println("\n请扫描书籍 ISBN 条码...");
  Serial.println("========================================\n");
}

void loop() {
  // ========== 接收条码数据 ==========
  while (barcodeSerial.available() > 0) {
    char c = barcodeSerial.read();

    if (c == '\n' || c == '\r') {
      if (barcodeData.length() > 0) {
        barcodeData.trim();  // 去除空白

        Serial.println("\n========== 收到 ISBN ==========");
        Serial.println(barcodeData);

        // ========== 查询书籍信息 ==========
        BookInfo book = lookupBookLocal(barcodeData);

        if (book.title.isEmpty()) {
          Serial.println("本地缓存未找到，正在联网查询...");
          book = lookupBookOnline(barcodeData);

          // 查询成功后保存到本地缓存
          if (!book.title.isEmpty()) {
            Serial.println("正在保存到本地缓存...");
            if (saveBookToCache(book)) {
              Serial.println(">>> 已缓存到本地");
            }
          }
        }

        // 打印结果
        Serial.println("\n========== 书籍信息 ==========");
        if (!book.title.isEmpty()) {
          printBookInfo(book);
        } else {
          Serial.println("未找到该书籍信息！");
        }
        Serial.println("================================\n");

        barcodeData = "";  // 清空缓冲区
      }
    } else {
      barcodeData += c;
    }
  }

  // 定期发送提示（可选）
  static unsigned long lastHint = 0;
  if (millis() - lastHint > 15000) {
    lastHint = millis();
    Serial.println("[提示] 等待扫描 ISBN 条码中...");
  }
}

// ============== WiFi 连接 ==============
void setupWiFi() {
  Serial.printf("连接到 WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi 连接成功!");
  Serial.print("IP 地址: ");
  Serial.println(WiFi.localIP());
}

// ============== 加载本地缓存 ==============
bool loadCache() {
  if (!SPIFFS.exists(CACHE_FILE)) {
    Serial.println("本地缓存文件不存在");
    File file = SPIFFS.open(CACHE_FILE, FILE_WRITE);
    if (file) {
      file.print("{}");
      file.close();
      Serial.println("已创建空缓存文件");
    }
    return false;
  }

  Serial.println("本地缓存文件已存在");
  return true;
}

// ============== 本地查询 ==============
BookInfo lookupBookLocal(const String& isbn) {
  BookInfo book;
  book.title = "";
  book.isbn = isbn;

  if (!SPIFFS.exists(CACHE_FILE)) {
    return book;
  }

  File file = SPIFFS.open(CACHE_FILE, FILE_READ);
  if (!file) {
    return book;
  }

  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();

  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, content);

  if (!error && doc.containsKey(isbn)) {
    JsonObject bookData = doc[isbn];
    book.title = bookData["title"].as<String>();
    book.author = bookData["author"].as<String>();
    book.publisher = bookData["publisher"].as<String>();
    book.publishYear = bookData["publishYear"].as<int>();
    Serial.println(">>> 从本地缓存找到书籍");
  }

  return book;
}

// ============== 保存到本地缓存 ==============
bool saveBookToCache(const BookInfo& book) {
  DynamicJsonDocument doc(8192);

  if (SPIFFS.exists(CACHE_FILE)) {
    File file = SPIFFS.open(CACHE_FILE, FILE_READ);
    if (file) {
      String content = "";
      while (file.available()) {
        content += (char)file.read();
      }
      file.close();

      DeserializationError error = deserializeJson(doc, content);
      if (error) {
        doc.clear();
      }
    }
  }

  JsonObject bookData = doc[book.isbn];
  bookData["title"] = book.title;
  bookData["author"] = book.author;
  bookData["publisher"] = book.publisher;
  bookData["publishYear"] = book.publishYear;

  File file = SPIFFS.open(CACHE_FILE, FILE_WRITE);
  if (!file) {
    return false;
  }

  size_t written = serializeJson(doc, file);
  file.close();

  Serial.printf("已写入 %d 字节到缓存\n", written);
  return written > 0;
}

// ============== 在线查询 ==============
BookInfo lookupBookOnline(const String& isbn) {
  BookInfo book;
  book.isbn = isbn;
  book.title = "";
  book.author = "";
  book.publisher = "";
  book.publishYear = 0;

  HTTPClient http;
  String url = "https://openlibrary.org/api/books?bibkeys=ISBN:" + isbn + "&format=json&jscmd=data";
  Serial.printf("查询 URL: %s\n", url.c_str());

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String key = "ISBN:" + isbn;
      if (doc.containsKey(key)) {
        JsonObject bookData = doc[key];

        if (bookData.containsKey("title")) {
          book.title = bookData["title"].as<String>();
        }

        if (bookData.containsKey("authors")) {
          JsonArray authors = bookData["authors"];
          for (int i = 0; i < authors.size(); i++) {
            if (i > 0) book.author += ", ";
            book.author += authors[i]["name"].as<String>();
          }
        }

        if (bookData.containsKey("publishers")) {
          book.publisher = bookData["publishers"][0]["name"].as<String>();
        }

        if (bookData.containsKey("publish_date")) {
          String publishDate = bookData["publish_date"].as<String>();
          for (int i = 0; i < publishDate.length(); i++) {
            if (isdigit(publishDate.charAt(i))) {
              String yearStr = "";
              for (int j = i; j < publishDate.length() && isdigit(publishDate.charAt(j)); j++) {
                yearStr += publishDate.charAt(j);
              }
              if (yearStr.length() == 4) {
                book.publishYear = yearStr.toInt();
                break;
              }
            }
          }
        }

        Serial.println(">>> 在线查询成功");
      } else {
        Serial.println("未找到该 ISBN 对应的书籍");
      }
    }
  } else {
    Serial.printf("HTTP 请求失败, 错误码: %d\n", httpCode);
  }

  http.end();
  return book;
}

// ============== 打印书籍信息 ==============
void printBookInfo(const BookInfo& book) {
  Serial.printf("ISBN:    %s\n", book.isbn.c_str());
  Serial.printf("书名:    %s\n", book.title.c_str());
  Serial.printf("作者:    %s\n", book.author.c_str());
  Serial.printf("出版社:  %s\n", book.publisher.c_str());
  if (book.publishYear > 0) {
    Serial.printf("出版年:  %d\n", book.publishYear);
  }
}
