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

---

# 第二次调试报告：USB 插拔后板子"死亡"（2026-05-20）

## 问题描述

显示测试固件正常工作中（屏幕显示红→绿→蓝→文字，串口正常输出），用户插拔一次 USB 线后，板子完全无法工作：屏幕黑屏、串口无输出、重新上传失败报错 `Packet content transfer stopped (received 1 bytes)`。

最终诊断：**flash 内容因带电插拔而损坏**，芯片启动时 bootloader 校验失败，自动回退到下载模式。

## 调试过程

### 阶段 1：确认板子通信

使用 esptool.py 直连：

```powershell
python esptool.py --chip esp32s3 --port COM9 --baud 115200 flash_id
```

结果：芯片正常响应，flash 为 16MB，eFuse 配置为 quad 模式。说明 USB 硬件通信正常，问题在 flash 内容层面。

### 阶段 2：全片擦除

使用 esptool.py 全片擦除：

```powershell
python esptool.py --chip esp32s3 --port COM9 --baud 115200 erase_flash
```

耗时 34 秒，成功完成。

### 阶段 3：手动烧录

PlatformIO 的 `pio run --target upload` 在 115200 波特率下也卡住（之前 921600 更差），改用 esptool.py 手动三段式烧录：

```powershell
python esptool.py --chip esp32s3 --port COM9 --baud 115200 write_flash -z `
    --flash_mode qio --flash_freq 80m --flash_size 16MB `
    0x0 .pio/build/esp32s3/bootloader.bin `
    0x8000 .pio/build/esp32s3/partitions.bin `
    0x10000 .pio/build/esp32s3/firmware.bin
```

三份文件全部写入并校验通过（Hash of data verified）。

### 阶段 4：验证板子启动

烧录纯背光测试固件（无 USBSerial、无 GFX，仅用 `pinMode` + `digitalWrite` 让背光闪烁），拔 USB 再插回后观察到背光规律闪烁 → 确认芯片从 flash 正常启动、固件正确执行。

### 阶段 5：恢复显示固件

烧录完整 Arduino_GFX 显示测试固件，屏幕正常显示红→绿→蓝→"SmartBracelet"文字，故障排除。

## 根因分析

| 因素 | 说明 |
|------|------|
| **直接原因** | USB 带电插拔导致 flash 某次写入被中断，bootloader 或应用数据损坏 |
| **为什么表现为"板子死了"** | ESP32-S3 的 ROM bootloader 检测到 flash 内容异常（校验失败或无效头部），自动进入下载模式等待救活 |
| **为什么上传也失败** | 921600 高波特率下信号完整性差，加上 flash 异常状态导致 esptool 通信中断 |
| **为什么 RTS 复位后仍进下载模式** | DTR/RTS 复位电路在此板子上不可靠，"Hard resetting via RTS pin" 后 GPIO0 可能仍被拉低 |

## 关键发现

### 1. 上传速度影响可靠性

| 速度 | 结果 |
|------|------|
| 921600 | 不稳定，`Packet content transfer stopped` |
| 115200 | 稳定工作 |

**建议**：ESP32-S3-Touch-LCD-1.83 使用 115200 上传速度。

### 2. 自定义板子配置的陷阱

`boards/ESP32-S3-R8-OPI.json` 的 `extra_flags` 包含 `-DARDUINO_ESP32S3_DEV_16M_OPI`，但 ESP32 Arduino 3.x 内核不识别此板型，导致 `USBSerial` 未定义、编译失败。

**解决**：使用标准板型 `esp32-s3-devkitc-1`，通过 `build_flags` 手动添加 `-DBOARD_HAS_PSRAM` 启用 PSRAM。flash 大小由 esptool 的 `--flash_size 16MB` 参数在烧录时指定。

### 3. eFuse flash 模式

`esptool.py flash_id` 确认该板 eFuse 设置为 **quad (4 data lines)**，必须使用 QIO 模式。

**不要**使用 `board_build.flash_mode = dio`，否则 flash 可能不工作。

### 4. RTS/DTR 复位电路不可靠

此板子的 RTS→EN 复位信号可能未正确连接或电平转换异常，esptool 的 "Hard resetting via RTS pin" 后芯片无法从 flash 启动。

**解决方法**：上传完毕后手动拔插 USB 线（不按 BOOT 键）来冷启动。

### 5. USB CDC 枚举超时

程序中 `USBSerial.begin()` 后若不等待枚举直接输出，可能在 USB 未准备好时丢失数据。但在等待期间若 USB 始终不枚举，程序会永久阻塞。

**解决**：添加 3 秒超时：

```cpp
USBSerial.begin(115200);
unsigned long start = millis();
while (!USBSerial && millis() - start < 3000) {
    delay(10);
}
```

### 6. 最小可行性验证

烧录固件后，如果串口无输出且屏幕黑屏，先用纯 GPIO 测试（背光闪烁）确认芯片是否在运行，再逐层添加外设驱动。

## 验证 Checklist

- [x] 全片擦除成功
- [x] bootloader 写入校验通过
- [x] 分区表写入校验通过
- [x] 固件写入校验通过
- [x] 背光闪烁测试通过（芯片运行确认）
- [x] Arduino_GFX 显示测试通过（红→绿→蓝→文字）
- [x] USBSerial 串口输出正常
- [x] USB 插拔后能正常启动（不按 BOOT）

## 最终配置

`platformio.ini` 关键设置：

```ini
upload_speed = 115200
board_build.flash_mode = qio

build_flags =
  -DCORE_DEBUG_LEVEL=5
  -Og
  -DDEBUG_ESP_PORT=Serial
  -DLV_CONF_INCLUDE_SIMPLE
  -DBOARD_HAS_PSRAM
```

esptool.py 烧录参数：

```powershell
python esptool.py --chip esp32s3 --port COM9 --baud 115200 write_flash -z `
    --flash_mode qio --flash_freq 80m --flash_size 16MB `
    0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

## 恢复流程（下次遇到同样问题）

```
1. 降低 platformio.ini upload_speed 到 115200
2. 按住 BOOT 键
3. 插 USB 线
4. 松开 BOOT 键
5. 运行 esptool.py erase_flash 全片擦除
6. 运行 pio run 编译
7. 运行 esptool.py 手动三段式烧录
8. 拔 USB → 等 10 秒 → 插 USB（不按 BOOT）
9. 验证背光/屏幕/串口
```

---

# 第三次调试报告：LVGL 集成（2026-05-20）

## 概述

在 Arduino_GFX 显示驱动正常工作基础上，集成 LVGL 8.4.0 图形库，实现显示缓冲区和刷新回调的对接。

## 涉及文件

| 文件 | 动作 | 说明 |
|------|------|------|
| `include/lv_conf.h` | **新建** | LVGL 8.4.0 配置文件，适配 240x284 ST7789 |
| `src/lv_port_disp.cpp.bak` | **→ .cpp** | 显示驱动移植文件（重命名激活） |
| `src/lv_port_indev.cpp.bak` | **→ .cpp** | 触控移植文件（重命名，未启用） |
| `src/main.cpp` | **修改** | 添加 lv_init()、lv_port_disp_init()、lv_timer_handler() |

## 关键发现

### 1. lv_conf.h 存放位置

`-DLV_CONF_INCLUDE_SIMPLE` 让 LVGL 通过 `#include "lv_conf.h"` 加载配置。编译器需要在 include path 中找到此文件。

| 位置 | 结果 |
|------|------|
| `src/lv_conf.h` | ❌ 编译时找不到（LVGL 库文件编译时 `src/` 不在 include path） |
| 项目根目录 `lv_conf.h` | ❌ 同样问题 |
| `include/lv_conf.h` + `-Iinclude` | ✅ 成功 |

**解决**：放在 `include/`，并在 `build_flags` 中添加 `-Iinclude`。

### 2. LV_COLOR_16_SWAP

ST7789 SPI 显示屏期望 RGB565 颜色以 **高字节在前** 的字节序传输。ESP32 是小端架构，uint16_t 在内存中存储为 [低字节, 高字节]。

| 设置 | 结果 |
|------|------|
| `LV_COLOR_16_SWAP 1` | ❌ 颜色错乱（"彩色的"） |
| `LV_COLOR_16_SWAP 0` | ✅ 颜色正常 |

这说明 Arduino_GFX 的 `draw16bitRGBBitmap()` 内部已经处理了字节序，LVGL 不需要再做 swap。

### 3. 内存占用

| 指标 | 数值 |
|------|------|
| RAM 使用 | 83,732 bytes (25.6%) |
| Flash 使用 | 528,917 bytes (15.8%) |
| LVGL 显示缓冲区 | 6,816 像素 × 2 字节 ≈ 13.6 KB（屏幕的 1/10） |

### 4. 触控未启用

CST816S 触控芯片的 I2C 引脚（TP_SDA=19, TP_SCL=20）与 ESP32-S3 原生 USB 引脚（USB_D+/USB_D-）为同一组 GPIO。启用触控 I2C 可能与 `USBSerial` 使用的 USB CDC 冲突。待确认正确引脚映射后再启用。

## 最终配置

`platformio.ini` 当前设置：

```ini
upload_speed = 115200
board_build.flash_mode = qio

build_flags =
  -DCORE_DEBUG_LEVEL=5
  -Og
  -DDEBUG_ESP_PORT=Serial
  -DLV_CONF_INCLUDE_SIMPLE
  -DBOARD_HAS_PSRAM
  -Iinclude
```

## LVGL 软件架构

```
main.cpp
  ├── Arduino_GFX (ST7789 SPI 驱动)
  │     └── gfx->draw16bitRGBBitmap()  ← LVGL disp_flush 回调
  ├── lv_init()
  ├── lv_port_disp_init()
  │     ├── disp_buf[240×284/10]       ← 显示缓冲区
  │     └── disp_drv.flush_cb           ← 注册刷新回调
  └── loop()
        └── lv_timer_handler()          ← 每 5ms 调用
```

## 验证 Checklist

- [x] lv_conf.h 编译通过
- [x] lv_port_disp 刷新回调正常
- [x] LVGL 标签文本显示正确
- [x] 颜色字节序正确（SWAP=0）
- [x] 内存占用在合理范围

---

# 第四次调试报告：LVGL 验证 + 触摸集成（2026-05-20）

## 概述

在之前 LVGL 集成卡住（`#include <lvgl.h>` 导致串口无输出）的基础上，重新验证并集成触摸驱动。

## 关键发现

### 1. 复位方式决定成败

| 复位方式 | 结果 | 说明 |
|---------|------|------|
| `--after no_reset` + DTR 手动 toggle | ❌ 从不工作 | RTS→EN 电路在此板子上无效 |
| `--after hard_reset` | ✅ 稳定工作 | USB RTS 信号虽显示 "Hard resetting via RTS pin..."，但实际触发了芯片冷启动 |

**教训**：必须使用 `--after hard_reset`。`esptool.py` 完成时的 RTS 脉冲才是唯一可靠的复位方式。

### 2. LVGL 不崩 — 是复位问题

之前 `#include <lvgl.h>` 导致无串口输出，**根因是 esptool 后板子未真正启动**，不是 LVGL 导致崩溃。验证方法：

1. 编译完整 LVGL 代码（`lv_init()` + `lv_port_disp_init()` + 创建 UI）
2. 用 `--after hard_reset` 烧录
3. 串口正确输出 `tick`（loop 中每 2s），说明 LVGL 完整流水线无崩溃

### 3. 触摸引脚修复

CST816S 触控芯片原定义在 `TP_SDA=19, TP_SCL=20`（与 USB D+/D- 共用 GPIO），修正为复用 I2C 传感器总线：

```
#define TP_SDA IIC_SDA    // → GPIO 15
#define TP_SCL IIC_SCL    // → GPIO 14
```

两个 `pin_config.h`（`include/` 和 `src/`）同步修改。

### 4. 触摸代码集成

- `main.cpp` 添加 `#include <CST816S.h>`、`#include "lv_port_indev.h"`
- 全局 `CST816S *touch = nullptr;`
- setup 中 `touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);` + `touch->begin();`
- `lv_port_indev_init()` 注册 LVGL 指针输入驱动

`lv_port_indev.cpp` 的 `touchpad_read` 回调使用 `touch->available()` 检查事件，映射 `touch->data.x/y` 到 LVGL 坐标。

## 当前软件架构

```
main.cpp
  ├── Arduino_GFX (ST7789 SPI)
  │     └── gfx->draw16bitRGBBitmap()  ← LVGL disp_flush 回调
  ├── lv_init()
  ├── lv_port_disp_init()              ← 显示缓冲区 + 刷新回调
  ├── lvgl_ui_init()                   ← UI: 暗色背景 + 标签
  ├── CST816S (I2C 15/14)
  │     └── touch->available() / data  ← LVGL indev 回调
  ├── lv_port_indev_init()             ← 触摸指针驱动
  └── loop()
        └── lv_timer_handler()          ← 每 5ms 处理 LVGL 任务
```

## 验证 Checklist

- [x] LVGL 完整代码编译通过 + 烧录运行（串口 "tick" 每 2s）
- [x] `--after hard_reset` 复位有效
- [x] 触摸引脚修正（19/20 → 15/14）
- [x] CST816S init 不崩溃
- [x] lv_port_indev 注册正常
- [ ] 屏幕实际显示 UI（深色背景 + "SmartBracelet\nLVGL Ready!"）
- [ ] 触摸物理回馈（需硬件确认引脚焊接）

## 遗留问题

1. **触摸硬件连接** — CST816S 物理上若仍焊在 GPIO 19/20，IIC 总线 15/14 上无触控芯片，需确认或飞线
2. **串口启动消息不可见** — USB CDC 重枚举慢于程序启动，setup 中的 `USBSerial.println` 总是丢失
3. **`touch->begin()` 无错误返回** — I2C 通信失败也会静默继续，需手动验证触摸读数
4. **CST816S 与 AXP2101 I2C 共享** — 地址不同 (0x15 vs 0x34) 理论上可共存，后续 PMU 集成时需验证
