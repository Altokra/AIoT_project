/*
  ESP32 MFRC522 NFC 模块 - 图书识别核心代码
  功能：读取 NFC 标签 UID，与图书信息对应
  使用 HSPI 接口，避免与 I2C 设备冲突

  引脚连接（HSPI 方案）：
  - SDA (CS):  GPIO 15
  - SCK:       GPIO 14
  - MOSI:      GPIO 13
  - MISO:      GPIO 12
  - RST:       GPIO 4
  - 3.3V:      3.3V
  - GND:       GND

  参考资料：https://randomnerdtutorials.com/esp32-mfrc522-rfid-reader-arduino/
*/

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

// ============== HSPI 引脚定义 ==============
#define HSPI_SDA_PIN   15   // 片选引脚 (CS)
#define HSPI_RST_PIN   4    // 复位引脚

// ============== 全局对象 ==============
MFRC522DriverPinSimple ss_pin(HSPI_SDA_PIN);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

// ============== 图书 UID 映射表 ==============
// 格式：UID -> 书名
// 实际项目中可替换为数据库查询
struct BookMapping {
  String uid;      // 卡片的 UID（十六进制字符串）
  String bookName; // 对应的书名
};

// 示例：已注册图书映射表
BookMapping bookTable[] = {
  {"01234abf", "《深入理解计算机系统》"},
  {"87654321", "《算法导论》"},
  {"abcdef01", "《设计模式》"}
};
const int BOOK_COUNT = sizeof(bookTable) / sizeof(BookMapping);

// ============== 状态追踪 ==============
String lastDetectedUID = "";  // 上次检测到的卡片 UID
bool bookOnShelf = false;    // 图书是否在架状态

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("========================================");
  Serial.println("  ESP32 NFC 图书识别系统初始化中...");
  Serial.println("========================================");

  // 初始化 MFRC522
  mfrc522.PCD_Init();

  // 打印模块版本信息
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);

  Serial.println("\nNFC 模块初始化完成");
  Serial.println("请将图书靠近读写器...\n");
}

void loop() {
  // 检测图书状态变化
  detectBook();

  delay(100);  // 避免过度频繁检测
}

/**
 * 检测图书并处理借出/归还事件
 */
void detectBook() {
  // 检查是否有新卡片靠近
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // 读取卡片序列号
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // 获取卡片 UID 并转换为字符串
  String currentUID = getUIDString();

  Serial.println("========== 检测到图书 ==========");
  Serial.print("卡片 UID: ");
  Serial.println(currentUID);

  // 查询图书信息
  String bookName = lookupBook(currentUID);
  Serial.print("图书名称: ");
  Serial.println(bookName);

  // 判断是借出还是归还（基于状态变化）
  if (currentUID != lastDetectedUID) {
    // 图书状态发生变化
    if (lastDetectedUID == "" && currentUID != "") {
      // 之前无书，现在有书 -> 归还
      Serial.println(">>> 图书已归还书架 <<<");
      onBookReturned(currentUID, bookName);
    } else if (lastDetectedUID != "" && currentUID == "") {
      // 之前有书，现在无书 -> 借出
      Serial.println(">>> 图书已被借出 <<<");
      onBookBorrowed(lastDetectedUID, lookupBook(lastDetectedUID));
    } else if (lastDetectedUID != "" && currentUID != "") {
      // 换书
      Serial.println(">>> 图书已更换 <<<");
      onBookBorrowed(lastDetectedUID, lookupBook(lastDetectedUID));
      onBookReturned(currentUID, bookName);
    }

    lastDetectedUID = currentUID;
  }

  Serial.println("================================\n");

  // 暂停卡片通信
  mfrc522.PICC_HaltA();
}

/**
 * 将 UID 字节数组转换为十六进制字符串
 */
String getUIDString() {
  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidStr += "0";  // 补零
    }
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();  // 转为大写
  return uidStr;
}

/**
 * 根据 UID 查找对应的图书名称
 * @param uid 卡片 UID
 * @return 图书名称，未找到返回"未知图书"
 */
String lookupBook(String uid) {
  for (int i = 0; i < BOOK_COUNT; i++) {
    if (bookTable[i].uid.equalsIgnoreCase(uid)) {
      return bookTable[i].bookName;
    }
  }
  return "未知图书 - 请先注册此标签";
}

/**
 * 图书被借出时的处理
 * 此处可添加 MQTT 上报逻辑
 */
void onBookBorrowed(String uid, String bookName) {
  Serial.println("[事件] 图书借出");
  Serial.print("  UID: ");
  Serial.println(uid);
  Serial.print("  书名: ");
  Serial.println(bookName);

  // TODO: 调用 MQTT 上报函数
  // reportBookStatus(uid, bookName, "borrowed");
}

/**
 * 图书归还时的处理
 * 此处可添加 MQTT 上报逻辑
 */
void onBookReturned(String uid, String bookName) {
  Serial.println("[事件] 图书归还");
  Serial.print("  UID: ");
  Serial.println(uid);
  Serial.print("  书名: ");
  Serial.println(bookName);

  // TODO: 调用 MQTT 上报函数
  // reportBookStatus(uid, bookName, "returned");
}

/**
 * 手动注册新图书标签
 * 当检测到新 UID 时调用此函数进行注册
 */
void registerNewBook(String uid, String bookName) {
  Serial.println("[注册] 发现新标签，已添加到映射表");
  Serial.print("  UID: ");
  Serial.println(uid);
  Serial.print("  书名: ");
  Serial.println(bookName);
  // 实际项目中应存储到 Flash 或发送至服务器保存
}
