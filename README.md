<!--
 * @Author: ka1shu1 cwh979946@163.com
 * @Date: 2026-04-13 18:30:12
 * @LastEditors: ka1shu1 cwh979946@163.com
 * @LastEditTime: 2026-04-18 15:44:58
 * @FilePath: \final project\README.md
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
-->



## 概述 


本项目为 2026 春夏学期 ldw 老师班 智能物联网系统设计大作业



## 参考项目

[智能图书馆](https://github.com/ECSLab/SLibrary)

- 可增加视觉（封面/条形码识别）功能（[ocr](https://github.com/CLHITw/OCR-character-recognition-system-of-charge-based-on-deep-learning)）

[火灾报警](https://github.com/mcu-coder/esp32_fire_and_smoke_alarm)
[环境监测](https://github.com/mcu-coder/esp32_environment_monitor)
感觉可照抄



## 构想

### 自然语言描述


1. 藏书追踪与借阅感知模块（核心交互）

功能： 抛弃传统的手工记录，实现图书的无感化管理。

实现： 为每一本书贴上 NTAG213 等低成本 NFC 标签。在书架边缘或书桌上嵌入 RFID 读写器（如 RC522 模块）配合微控制器（如 ESP32）。当书被放上或拿走时，系统自动识别图书 ID，并通过 MQTT 协议将借阅/归还状态实时推送到后端。

我们每新买一本书，就可以先用摄像头获取书籍图像，结合ocr和大数据（或者api爬取）填写相关信息，然后贴上NFC标签，从而实现记录图书状态功能，再通过云端同步数据，小程序随时记录读书想法等等


### 一些问题

#### 既然有ISBN，还要NFC干什么？

无需每次取放图书都扫码，大大方便了我们取阅时的记录


#### 对于监测图书是否在书架上的程序，我们会产生海量的数据

维护一个心跳包，存放书籍是否在架的信息，只有当本信息发生变化时才上报云端服务器，产生的数据很少


####



### 参考资料

- [ESP32 with MFRC522 RFID Reader/Writer (Arduino IDE)](https://randomnerdtutorials.com/esp32-mfrc522-rfid-reader-arduino/)





### 硬件部分

- Ntag213
- RC522