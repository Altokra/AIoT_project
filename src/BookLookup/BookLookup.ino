/*
  ESP32 书籍信息查询程序 - 增强版
  功能：
  1. 扫描 ISBN 条码查询书籍信息
  2. 本地缓存未找到时联网查询
  3. 自动将查询结果缓存到 Flash（SPIFFS）
  4. 下次查询时优先从本地缓存读取

  ISBN 条码查询示例：
  - 9787115546321 -> 《算法导论》
  - 9787121354289 -> 《深入理解计算机系统》
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// ============== WiFi 配置 ==============
const char* ssid     = "你的WiFi名称";
const char* password = "你的WiFi密码";

// ============== 书籍信息结构 ==============
struct BookInfo {
  String isbn;
  String title;
  String author;
  String publisher;
  int publishYear;
};

// ============== 本地缓存配置 ==============
const char* CACHE_FILE = "/book_cache.json";  // 缓存文件名

// ============== 全局变量 ==============
WiFiClient client;

// ============== 函数声明 ==============
void setupWiFi();
bool loadCache();
bool saveBookToCache(const BookInfo& book);
BookInfo lookupBookLocal(const String& isbn);
BookInfo lookupBookOnline(const String& isbn);
void printBookInfo(const BookInfo& book);

void setup() {
  Serial.begin(115200);

  Serial.println("========================================");
  Serial.println("  ESP32 书籍查询系统 (自动缓存版)");
  Serial.println("========================================");

  // 初始化 SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 初始化失败!");
    return;
  }
  Serial.println("SPIFFS 初始化成功");

  // 加载本地缓存
  loadCache();

  // 连接 WiFi
  setupWiFi();

  Serial.println("\n请扫描书籍 ISBN 条码...");
  Serial.println("========================================\n");
}

void loop() {
  // 模拟收到条码数据（实际使用时替换为 barcodeSerial 读取）
  static String testISBN = "";
  static unsigned long lastTest = 0;

  // 每 10 秒自动测试一次（实际使用时删除这段）
  if (millis() - lastTest > 10000) {
    lastTest = millis();
    testISBN = "9787115546321";  // 测试用 ISBN
    Serial.println("\n========== 收到 ISBN ==========");
    Serial.println(testISBN);

    // 1. 先查本地缓存
    BookInfo book = lookupBookLocal(testISBN);

    // 2. 本地没有则联网查询
    if (book.title.isEmpty()) {
      Serial.println("本地缓存未找到，正在联网查询...");
      book = lookupBookOnline(testISBN);

      // 3. 查询成功后保存到本地缓存
      if (!book.title.isEmpty()) {
        Serial.println("正在保存到本地缓存...");
        if (saveBookToCache(book)) {
          Serial.println(">>> 已缓存到本地");
        } else {
          Serial.println(">>> 缓存失败（可能是存储空间不足）");
        }
      }
    }

    // 4. 打印结果
    Serial.println("\n========== 书籍信息 ==========");
    if (!book.title.isEmpty()) {
      printBookInfo(book);
    } else {
      Serial.println("未找到该书籍信息！");
    }
    Serial.println("================================\n");
  }
}

/**
 * 连接 WiFi
 */
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

/**
 * 加载本地缓存文件
 */
bool loadCache() {
  if (!SPIFFS.exists(CACHE_FILE)) {
    Serial.println("本地缓存文件不存在，将创建新文件");
    // 创建一个空的 JSON 对象
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

/**
 * 从本地缓存查找书籍
 */
BookInfo lookupBookLocal(const String& isbn) {
  BookInfo book;
  book.title = "";
  book.isbn = isbn;

  if (!SPIFFS.exists(CACHE_FILE)) {
    return book;
  }

  // 读取缓存文件
  File file = SPIFFS.open(CACHE_FILE, FILE_READ);
  if (!file) {
    return book;
  }

  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();

  // 解析 JSON
  DynamicJsonDocument doc(8192);  // 增大缓存以支持更多书籍
  DeserializationError error = deserializeJson(doc, content);

  if (!error) {
    // 尝试获取该 ISBN 的书籍信息
    if (doc.containsKey(isbn)) {
      JsonObject bookData = doc[isbn];

      book.title = bookData["title"].as<String>();
      book.author = bookData["author"].as<String>();
      book.publisher = bookData["publisher"].as<String>();
      book.publishYear = bookData["publishYear"].as<int>();

      Serial.println(">>> 从本地缓存找到书籍");
    }
  }

  return book;
}

/**
 * 保存书籍到本地缓存
 */
bool saveBookToCache(const BookInfo& book) {
  // 读取现有缓存
  DynamicJsonDocument doc(8192);

  if (SPIFFS.exists(CACHE_FILE)) {
    File file = SPIFFS.open(CACHE_FILE, FILE_READ);
    if (file) {
      String content = "";
      while (file.available()) {
        content += (char)file.read();
      }
      file.close();

      // 解析现有数据
      DeserializationError error = deserializeJson(doc, content);
      if (error) {
        // 解析失败则创建新文档
        doc.clear();
      }
    }
  }

  // 添加新书籍到缓存
  JsonObject bookData = doc[book.isbn];
  bookData["title"] = book.title;
  bookData["author"] = book.author;
  bookData["publisher"] = book.publisher;
  bookData["publishYear"] = book.publishYear;

  // 写入文件
  File file = SPIFFS.open(CACHE_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("无法打开缓存文件写入!");
    return false;
  }

  size_t written = serializeJson(doc, file);
  file.close();

  Serial.printf("已写入 %d 字节到缓存\n", written);
  return written > 0;
}

/**
 * 在线查询书籍信息（使用 Open Library API）
 */
BookInfo lookupBookOnline(const String& isbn) {
  BookInfo book;
  book.isbn = isbn;
  book.title = "";
  book.author = "";
  book.publisher = "";
  book.publishYear = 0;

  HTTPClient http;

  // 构建 API URL
  String url = "https://openlibrary.org/api/books?bibkeys=ISBN:" + isbn + "&format=json&jscmd=data";
  Serial.printf("查询 URL: %s\n", url.c_str());

  http.begin(client, url);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("API 响应成功");

    // 解析 JSON
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String key = "ISBN:" + isbn;
      if (doc.containsKey(key)) {
        JsonObject bookData = doc[key];

        // 提取书名
        if (bookData.containsKey("title")) {
          book.title = bookData["title"].as<String>();
        }

        // 提取作者
        if (bookData.containsKey("authors")) {
          JsonArray authors = bookData["authors"];
          for (int i = 0; i < authors.size(); i++) {
            if (i > 0) book.author += ", ";
            book.author += authors[i]["name"].as<String>();
          }
        }

        // 提取出版社
        if (bookData.containsKey("publishers")) {
          book.publisher = bookData["publishers"][0]["name"].as<String>();
        }

        // 提取出版年份
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
    } else {
      Serial.print("JSON 解析失败: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.printf("HTTP 请求失败, 错误码: %d\n", httpCode);
  }

  http.end();
  return book;
}

/**
 * 打印书籍信息
 */
void printBookInfo(const BookInfo& book) {
  Serial.printf("ISBN:    %s\n", book.isbn.c_str());
  Serial.printf("书名:    %s\n", book.title.c_str());
  Serial.printf("作者:    %s\n", book.author.c_str());
  Serial.printf("出版社:  %s\n", book.publisher.c_str());
  if (book.publishYear > 0) {
    Serial.printf("出版年:  %d\n", book.publishYear);
  }
}
