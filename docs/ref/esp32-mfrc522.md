# ESP32 MFRC522 RFID 模块教程

> **来源**：[Random Nerd Tutorials - ESP32 RFID Sensor Tutorial](https://randomnerdtutorials.com/esp32-mfrc522-rfid-reader-arduino/)

## 概述

MFRC522 RFID 读写器工作电压为 3.3V，支持 SPI 和 I2C 两种通信协议。本教程使用 SPI 通信方式。

---

## 硬件准备

### 所需元器件

| 元器件 | 说明 |
|--------|------|
| ESP32 DOIT DEVKIT V1 | 开发板 |
| MFRC522 RFID 读写器 + 标签 | 射频识别模块 |
| 面包板 | 用于搭建电路 |
| 跳线 | 连接线路 |

---

## ESP32 与 MFRC522 引脚连接

MFRC522 与 ESP32 默认 SPI 引脚连接表：

| MFRC522 引脚 | ESP32 GPIO | 功能说明 |
|-------------|------------|----------|
| SDA | GPIO 5 | SPI 片选信号输入 |
| SCK | GPIO 18 | SPI 时钟线 |
| MOSI | GPIO 23 | SPI 数据输入 |
| MISO | GPIO 19 | SPI 数据输出 |
| IRQ | 不连接 | 中断引脚（标签靠近时触发） |
| GND | GND | 电源地 |
| RST | GPIO 21 | 复位引脚（低电平断电，高电平复位） |
| 3.3V | 3.3V | 电源正极（2.5-3.3V） |

---

## MIFARE 1K 标签内存结构

### 内存布局

- **总容量**：16 个扇区 × 4 个块/扇区 × 16 字节/块 = 1024 字节（1KB）
- **实际可用**：752 字节（每扇区最后一块为权限控制块）

### 扇区结构

| 块号 | 类型 | 说明 |
|------|------|------|
| 0 | 制造商块 | 存储 UID（只读，不可修改） |
| 1~3 | 数据块 | 可存储用户数据 |
| 4 (每个扇区最后一块) | 扇区尾块 | 存储两个密钥（KEY_A 和 KEY_B）及访问权限控制位 |

### 重要提示

> **警告**：每扇区的最后一块（扇区尾块）存储密钥和权限信息，**请勿随意写入**，否则可能导致卡片永久损坏！

---

## 代码示例

### 示例一：读取 RFID 卡原始数据

读取标签的完整信息（UID、类型、内存块数据）。

```cpp
/*
  Rui Santos & Sara Santos - Random Nerd Tutorials
  完整项目详情：https://RandomNerdTutorials.com/esp32-mfrc522-rfid-reader-arduino/
*/

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
//#include <MFRC522DriverI2C.h>  // 如需使用 I2C，请取消注释
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

// 定义 SPI 片选引脚为 GPIO 5
MFRC522DriverPinSimple ss_pin(5);

MFRC522DriverSPI driver{ss_pin}; // 创建 SPI 驱动实例
//MFRC522DriverI2C driver{};     // 如需使用 I2C，请创建 I2C 驱动实例
MFRC522 mfrc522{driver};         // 创建 MFRC522 实例

void setup() {
  Serial.begin(115200);  // 初始化串口通信，波特率 115200
  while (!Serial);       // 等待串口连接（适用于 ATMEGA32U4 等开发板）

  mfrc522.PCD_Init();    // 初始化 MFRC522 模块
  // 打印 PCD（MFRC522）版本信息到串口
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);
  Serial.println(F("请将 PICC 靠近读写器以查看 UID、SAK、类型和数据块..."));
}

void loop() {
  // 检查是否有新卡片靠近读写器
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;  // 无新卡片，返回继续检测
  }

  // 选择其中一张卡片并读取其序列号
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;  // 读取失败，返回
  }

  // 打印卡片的完整调试信息（包括 UID、SAK、类型和所有数据块）
  MFRC522Debug::PICC_DumpToSerial(mfrc522, Serial, &(mfrc522.uid));

  delay(2000);  // 延时 2 秒，避免频繁读取
}
```

---

### 示例二：读取 RFID 卡 UID

仅读取卡片的唯一标识符（UID），适用于只需识别卡片身份的简单场景。

```cpp
/*
  Rui Santos & Sara Santos - Random Nerd Tutorials
*/

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

void setup() {
  Serial.begin(115200);
  while (!Serial);

  mfrc522.PCD_Init();    // 初始化 MFRC522 模块
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);
  Serial.println(F("请将 PICC 靠近读写器以查看 UID"));
}

void loop() {
  // 检测是否有新卡片
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // 读取卡片序列号
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // 打印 UID（格式：XX XX XX XX）
  Serial.print("Card UID: ");
  MFRC522Debug::PrintUID(Serial, (mfrc522.uid));
  Serial.println();

  // 将 UID 转换为十六进制字符串并保存到变量
  String uidString = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidString += "0"; // 小于 0x10 的字节前面补 0
    }
    uidString += String(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println(uidString);  // 输出十六进制字符串形式的 UID
}
```

---

### 示例三：向 RFID 卡写入和读取数据

向指定块写入自定义数据，并读取验证。

```cpp
/*
  Rui Santos & Sara Santos - Random Nerd Tutorials
*/

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

MFRC522::MIFARE_Key key;  // 定义密钥结构体

// 要写入的块地址（0~63，但不能是扇区尾块）
byte blockAddress = 2;

// 要写入的数据（最多 16 字节）
byte newBlockData[17] = {"Rui Santos - RNT"};
// 如需清空数据块，可使用：
// byte newBlockData[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

byte bufferblocksize = 18;  // 读取缓冲区大小（16字节数据 + 2字节状态）
byte blockDataRead[18];     // 读取数据缓冲区

void setup() {
  Serial.begin(115200);
  while (!Serial);

  mfrc522.PCD_Init();    // 初始化 MFRC522 模块
  Serial.println(F("警告：此示例会写入卡片数据，请谨慎操作！"));

  // 初始化密钥：出厂默认密钥为 FFFFFFFFFFFF
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

void loop() {
  // 检测是否有新卡片并读取其序列号
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    delay(500);
    return;
  }

  // 显示卡片 UID
  Serial.print("----------------\nCard UID: ");
  MFRC522Debug::PrintUID(Serial, (mfrc522.uid));
  Serial.println();

  // 使用 KEY_A（0x60）对指定块进行身份认证
  if (mfrc522.PCD_Authenticate(0x60, blockAddress, &key, &(mfrc522.uid)) != 0) {
    Serial.println("身份认证失败！");
    return;
  }

  // 向指定块写入数据
  if (mfrc522.MIFARE_Write(blockAddress, newBlockData, 16) != 0) {
    Serial.println("写入失败！");
  } else {
    Serial.print("数据成功写入块 ");
    Serial.println(blockAddress);
  }

  // 再次认证（读取前需要重新认证）
  if (mfrc522.PCD_Authenticate(0x60, blockAddress, &key, &(mfrc522.uid)) != 0) {
    Serial.println("身份认证失败！");
    return;
  }

  // 从指定块读取数据
  if (mfrc522.MIFARE_Read(blockAddress, blockDataRead, &bufferblocksize) != 0) {
    Serial.println("读取失败！");
  } else {
    Serial.println("读取成功！");
    Serial.print("块 ");
    Serial.print(blockAddress);
    Serial.print(" 中的数据：");
    for (byte i = 0; i < 16; i++) {
      Serial.print((char)blockDataRead[i]);  // 以字符形式打印
    }
    Serial.println();
  }

  // 暂停与卡片的通信并停止加密
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(2000);  // 延时以便观察串口输出
}
```

---

## 代码工作流程说明

### 身份认证机制

| 参数 | 说明 |
|------|------|
| `0x60` | 使用 KEY_A 进行身份认证 |
| `0x61` | 使用 KEY_B 进行身份认证 |
| 默认密钥 | `FF FF FF FF FF FF`（出厂设置）|

### 操作流程

```
检测卡片 → 读取序列号 → 身份认证 → 读写数据 → 停止通信
```

### 关键函数说明

| 函数 | 功能 |
|------|------|
| `PCD_Init()` | 初始化 MFRC522 模块 |
| `PICC_IsNewCardPresent()` | 检测是否有新卡片靠近 |
| `PICC_ReadCardSerial()` | 读取卡片序列号 |
| `PCD_Authenticate()` | 对指定块进行身份认证 |
| `MIFARE_Write()` | 向指定块写入数据 |
| `MIFARE_Read()` | 从指定块读取数据 |
| `PICC_HaltA()` | 暂停卡片通信 |
| `PCD_StopCrypto1()` | 停止加密通信 |

---

## 其他有用示例

Arduino_MFRC522v2 库还提供以下示例（位于 Hack 子菜单）：

- **ChangeUID**：修改卡片 UID（仅适用于支持写入 UID 的特殊 MIFARE Classic 卡）
- **FixBrickedUID**：修复因误操作损坏的卡片 UID

> **注意**：大多数 RFID 卡片的 UID 是只读的，无法修改。上述示例仅对支持可写 UID 的特殊卡片有效。
