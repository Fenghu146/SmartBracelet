# ESP32-S3-Touch-LCD-1.83 白屏故障调试报告

## 问题描述

Waveshare ESP32-S3-Touch-LCD-1.83 开发板，PlatformIO 项目编译烧录成功，但屏幕始终无显示（白屏/背光亮无内容），仅在上传瞬间有闪现。

## 硬件环境

| 项目  | 规格                                           |
| --- | -------------------------------------------- |
| 主控  | ESP32-S3, 240MHz, 16MB Flash, 8MB OPI PSRAM  |
| 屏幕  | ST7789, 240×284, SPI 接口                      |
| 连接  | USB (COM9), HWCDC (USB Serial)               |
| 环境  | PlatformIO + espressif32@6.9.0 + Arduino 3.x |

## 调试过程

### 第 1 阶段：确认 Boot Loop

最小化串口测试代码后发现串口无输出。使用 `pio device monitor` 观察到：

```
rst:0x3 (RTC_SW_SYS_RST)
```

每 ~200ms 重复一次。ESP32-S3 在 `setup()` 执行前不断重启，代码从未到达用户程序。

### 第 2 阶段：定位 Boot Loop 根因

查看编译输出发现链接错误：

```
multiple definition of `USBSerial'
     main.cpp.o
     HWCDC.cpp.o
```

**根因**：在 `src/main.cpp` 中手写了：

```cpp
#include "HWCDC.h"
HWCDC USBSerial;
```

而 ESP32 Arduino 3.x 核心的 `HWCDC.cpp` 已预定义了全局 `USBSerial` 对象。重复定义导致启动时内存布局错误，触发 `RTC_SW_SYS_RST` 无限重启。

**修复**：删除 `#include "HWCDC.h"` 和 `HWCDC USBSerial;`，直接使用框架内置的 `USBSerial`。

### 第 3 阶段：恢复串口输出

移除重复定义后，最小串口测试正常输出：

```
Hello World - board is alive!
```

每 2 秒重复，证明板子正常启动。

### 第 4 阶段：屏幕显示无法工作

添加 `Arduino_GFX` 库的显示代码后，串口无输出、屏幕无显示。

初次写法（**错误**）：

```cpp
// 全局构造 — 在 setup() 前执行
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, ...);
Arduino_GFX *gfx = new Arduino_ST7789(bus, ...);
```

**原因**：Arduino 启动流程中，全局对象的构造函数在 `init()` 之后、`setup()` 之前执行。`new Arduino_ESP32SPI` 在堆管理器可能尚未完全就绪的阶段分配内存，或 SPI 外设在 core 初始化完成前被触碰，导致静默崩溃，串口驱动也未初始化，因此看不到任何输出。

**修复**：全局只声明指针，在 `setup()` 内 `new`：

```cpp
Arduino_DataBus *bus = NULL;
Arduino_GFX *gfx = NULL;

void setup() {
    USBSerial.begin(115200);
    bus = new Arduino_ESP32SPI(LCD_DC, ...);
    gfx = new Arduino_ST7789(bus, ...);
    ...
}
```

### 第 5 阶段：屏幕验证

烧录修复后的代码，观察到串口输出完整流程：

```
Booting...
bus created
gfx created
gfx->begin() OK
RED
GREEN
BLUE
DONE
```

屏幕依次显示 红 → 绿 → 蓝 全屏，最后黑底白字显示 "SmartBracelet"。

## 关键发现

### 1. ESP32 Arduino 3.x 的 USBSerial

| 版本                | 行为                                    |
| ----------------- | ------------------------------------- |
| ESP32 Arduino 2.x | `USBSerial` 通常由用户定义                   |
| ESP32 Arduino 3.x | `USBSerial` 已在 `HWCDC.cpp` 预定义，直接使用即可 |

不要在任何文件中包含 `HWCDC.h` 并定义 `HWCDC USBSerial;`。

### 2. 全局对象初始化时机

Arduino 程序启动顺序：

```
initVariant() → 全局构造 → setup() → loop()
```

`new` 表达式在全局构造阶段执行时，以下设施可能不可靠：

- 堆管理器（部分初始化）
- 外设寄存器（SPI/UART 等）
- USB CDC 驱动

**规则**：C++ 对象的 `new` 操作应在 `setup()` 内执行，全局作用域只声明指针。

### 3. 编译正确 ≠ 运行正确

两次问题（重复 USBSerial、全局 `new`）均能通过编译，只在运行时崩溃且无可见错误信息。必须在串口输出最低限度诊断信息来定位。

### 4. PlatformIO 输出截断

`rtk pio run --target upload` 的输出在 esptool 开始上传处截断（~51200 bytes 限制），使用直接调用 esptool.py 上传更可靠：

```powershell
python "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" `
    --chip esp32s3 --port COM9 --baud 921600 `
    --before default_reset --after hard_reset write_flash -z `
    --flash_mode dio --flash_freq 80m --flash_size detect `
    0x0 ".pio/build/esp32s3/bootloader.bin" `
    0x8000 ".pio/build/esp32s3/partitions.bin" `
    0x10000 ".pio/build/esp32s3/firmware.bin"
```

### 5. 板子自动复位

某些 ESP32-S3 板子的 RTS 引脚未连接至复位线路，esptool 的 "Hard resetting via RTS pin..." 可能无效。此时需手动按 RST 按键，或通过 DTR 信号复位：

```powershell
$port = new-Object System.IO.Ports.SerialPort COM9,115200
$port.DtrEnable = $true
$port.Open()
Start-Sleep -Milliseconds 200
$port.DtrEnable = $false
$port.Close()
```

## 最终引脚配置

| 信号       | 引脚  |
| -------- | --- |
| LCD_DC   | 4   |
| LCD_CS   | 5   |
| LCD_SCK  | 6   |
| LCD_MOSI | 7   |
| LCD_RST  | 38  |
| LCD_BL   | 40  |
| TP_SDA   | 19  |
| TP_SCL   | 20  |
| TP_INT   | 21  |
| TP_RST   | 39  |
| IIC_SDA  | 15  |
| IIC_SCL  | 14  |

## 经验教训

1. **最少诊断优先** — 遇到白屏，先跑纯串口输出确认板子启动，再逐层添加显示代码。
2. **警惕全局构造** — 嵌入式 C++ 中全局 `new` 是不安全的，始终在 `setup()` 内分配。
3. **阅读核心源码** — ESP32 Arduino 3.x 与 2.x 有显著差异（如 USBSerial 预定义），迁移项目时应查看 `HWCDC.cpp` 等核心文件。
4. **串口日志是生命线** — 在每一步关键操作前后添加 `USBSerial.println()` 输出，确保程序执行流可见。
5. **ESP32-S3 的 USB 串口** — 不依赖 `Serial`（通过 UART0 的物理串口），而是使用 `USBSerial`（通过 USB CDC 的虚拟串口）。
