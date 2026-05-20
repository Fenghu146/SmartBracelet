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

---

# 第五次调试报告：触摸驱动迁移至 Arduino_DriveBus + Boot Loop（2026-05-20）

## 概述

发现官方 Waveshare 板使用 **CST816D** 芯片（非 CST816S），需从 `fbiego/CST816S` 库迁移至 `Arduino_DriveBus` 官方库。迁移编译成功但烧录后进入 **Boot Loop**（TG0WDT_SYS_RST 无限重启）。

## 关键发现

### 1. 芯片型号更正

| 来源 | 声称型号 | 实际型号 |
|------|---------|---------|
| 旧 `CST816S` 库 | CST816S | ❌ 错误 |
| Waveshare 官方仓库 + schematic | CST816T/D | ✅ CST816D (I2C 地址 0x15) |
| `Arduino_DriveBus` 驱动 | CST816D/T | ✅ 同时支持 CST816D 和 CST816T |

**结论**：板子上的触控芯片是 **CST816D**，必须使用 `Arduino_DriveBus` 库的 `Arduino_CST816x` 驱动。

### 2. 引脚配置更正

对比官方 Waveshare `pin_config.h`（确认无 `TP_SDA`/`TP_SCL` 定义）：

```cpp
// 旧（错误）:
#define TP_SDA IIC_SDA  // 19 (或 15)
#define TP_SCL IIC_SCL  // 20 (或 14)

// 新（匹配官方）:
// 无 TP_SDA/TP_SCL 定义
// 触控 I2C 通过 IIC_SDA (15) / IIC_SCL (14) + Wire.begin() 访问
```

### 3. Arduino_DriveBus API

| 功能 | API |
|------|-----|
| 总线 | `std::make_shared<Arduino_HWIIC>(sda, scl, &Wire)` |
| 触控对象 | `std::make_unique<Arduino_CST816x>(bus, 0x15, rst, irq, callback)` |
| 中断标志 | `touch->IIC_Interrupt_Flag`（ISR 中置位） |
| 读坐标 X | `touch->IIC_Read_Device_Value(Arduino_IIC_Touch::TOUCH_COORDINATE_X)` |
| 读坐标 Y | `touch->IIC_Read_Device_Value(Arduino_IIC_Touch::TOUCH_COORDINATE_Y)` |
| 写中断模式 | `touch->IIC_Write_Device_State(Arduino_IIC_Touch::TOUCH_DEVICE_INTERRUPT_MODE, Arduino_IIC_Touch::TOUCH_DEVICE_INTERRUPT_PERIODIC)` |

### 4. Boot Loop 分析

迁移后编译成功，烧录后观察串口：

```
SHA-256 comparison failed:
Attempting to boot anyway...
entry 0x403774a0
→ ~800ms → rst:0x7 (TG0WDT_SYS_RST) → 重复
```

SHA-256 校验失败是已有现象（eFuse secure boot hash 不匹配），之前一直 `Attempting to boot anyway...` 后正常启动。本次区别：

| 指标 | 第4次（正常） | 第5次（Boot Loop） |
|------|-------------|-------------------|
| RAM 使用 | 38,776 bytes (11.8%) | 90,040 bytes (27.5%) |
| Flash 使用 | 680,691 bytes (20.3%) | 770,181 bytes (23.0%) |
| `.iram0.text` | 未知 | 60,523 bytes (0xec6b) |
| `.dram0.bss` | 未知 | 75,720 bytes (0x127c8) |

**RAM 翻倍**（38KB → 90KB），IRAM 使用 60KB。`esp32-s3-devkitc-1` 默认 IRAM 上限接近此值，可能导致启动阶段内存布局冲突。

**可能原因**：
1. IRAM 溢出（60KB 超过默认 48KB/64KB 限制）
2. `-DBOARD_HAS_PSRAM` 与无 PSRAM 板冲突
3. `SensorLib` 的 `TouchDrvCST816` 与 `Arduino_CST816x` 双驱动冲突
4. `-DCORE_DEBUG_LEVEL=5` 引入过多 `IRAM_ATTR` 函数

### 5. 擦除后串口丢失

`esptool.py erase_flash` 成功后芯片彻底无 bootloader，串口设备消失（COM9 不可访问）。需进入下载模式（BOOT+RST）或冷启动才能重新识别。

### 6. 当前状态

- 触控驱动迁移 **代码完成、编译通过**
- **烧录后 Boot Loop**，无法验证触摸功能
- flash 已擦除，板子空片待救

## 下一步

1. 冷启动或进入下载模式后重新烧录
2. 如仍 Boot Loop：移除 `-DBOARD_HAS_PSRAM` 和 `-DCORE_DEBUG_LEVEL=5` 缩小镜像
3. 或使用 `ESP32-S3-R8-OPI` 自定义板 JSON（带 OPI PSRAM，IRAM 空间更大）
4. 最终验证：`pio device monitor` 应打印 `T x y` 触摸坐标 + `tick` 每 2s

## 后续修复：回退 CST816S + 精简依赖

### 根因

`Arduino_DriveBus` 库体积过大（+244KB Flash），导致 IRAM 溢出和 TG0WDT 复位。`fbiego/CST816S` 库足够驱动 CST816D 芯片（I2C 地址同为 0x15）。

### 改动

| 文件 | 变化 |
|------|------|
| `platformio.ini` | 删 `SensorLib`、`Arduino_DriveBus`、`DCORE_DEBUG_LEVEL=5`，加 `CST816S` |
| `src/main.cpp` | 回退到 `CST816S` 库 API，保留 LVGL 和触控 |
| `src/lv_port_indev.cpp/h` | 回退到 `CST816S` 库 API |
| `include/pin_config.h` | 恢复 `TP_SDA IIC_SDA`、`TP_SCL IIC_SCL` |
| `lib/Arduino_DriveBus/` | 删除 |

### 结果

| 指标 | 修复前（DriveBus） | 修复后（CST816S） |
|------|------------------|------------------|
| RAM | 90,040 bytes (27.5%) | 84,108 bytes (25.7%) |
| Flash | 770,181 bytes (23.0%) | 524,165 bytes (15.7%) |
| 板子启动 | ❌ Boot Loop | ✅ 正常 |
| 触控通信 | ❌ 无法验证 | ✅ `T x y ev=0/1` |
| LVGL 显示 | ❌ | ✅ |
| 设备管理器 | 不断刷新 | ✅ 稳定

---

# 第六次调试报告：传感器集成 + 表盘 UI（2026-05-20）

## 概述

在显示+触控稳定的基础上，完成全部 I2C 传感器驱动集成，并搭建 LVGL 表盘 UI。

## 传感器驱动

四个 I2C 设备共用总线 GPIO15/14，地址无冲突：

| 地址 | 设备 | 驱动方式 | 状态 |
|------|------|---------|------|
| 0x15 | CST816D 触控 | `fbiego/CST816S` 库 | ✅ |
| 0x51 | PCF85063 RTC | Waveshare SensorLib `SensorPCF85063.hpp` | ✅ |
| 0x6A | QMI8658 IMU | Waveshare SensorLib `SensorQMI8658.hpp` | ✅ |
| 0x34 | AXP2101 PMU | XPowersLib `XPowersAXP2101.tpp` | ✅ |

### 关键决策

- **传感器库**：从 Waveshare 官方仓库复制 `SensorLib` 到 `lib/SensorLib-Waveshare/`，只包含需要的头文件，避免 IRAM 膨胀
- **PMU 库**：从 ESP-IDF 例子复制 `XPowersLib`，支持 Arduino `TwoWire` 接口
- **I2C 顺序**：`Wire.begin()` 在传感器 init 前调用，确保总线就绪

## LVGL 表盘 UI

### 设计

```
┌──────────────┐
│   ●   ○      │  ← 页面指示器
│      BAT 0%  │
│              │
│    19:00     │  ← 时间 (Montserrat 32)
│              │
│  Wed May 20  │  ← 日期
│              │
│  Steps: 123  │  ← 步数
└──────────────┘
```

### 多页面导航

| 功能 | 实现 |
|------|------|
| 页面切换 | `lv_scr_load_anim()` 左右滑动动画 |
| 手势检测 | CST816S `gestureID` (SWIPE_LEFT / SWIPE_RIGHT) |
| 页面数 | 2（表盘 + 传感器数据） |

### 步数计数

简单峰值检测算法：加速度合量超过静止值 (1g) + 阈值时计数一次，回落后再计数下次。

### 资源占用

| 指标 | 数值 |
|------|------|
| RAM | 100,936 bytes (30.8%) |
| Flash | 604,817 bytes (18.1%) |
| 字体 | Montserrat 12/14/16/24/32 |

## 当前验证 Checklist

- [x] RTC 时间显示正常
- [x] IMU 加速度/陀螺仪实时读数
- [x] PMU 电池百分比
- [x] 触控左右滑动切换页面
- [x] 数字表盘布局正确
- [x] 步数计数（基础版）
- [ ] 低功耗模式
- [ ] 手机 App

---

# 第七次调试报告：BLE 双向通信 + 中文支持（2026-05-20）

## 概述

10 小时密集开发，完成了步数优化、模拟表盘、WiFi NTP、BLE 双向通信的全部集成。

## 完成内容

| 时间段 | 内容 |
|--------|------|
| 1-2h | 步数算法优化（低通滤波 + 自适应基线） |
| 2-3h | 模拟指针表盘（时分秒针 + 刻度） |
| 3-4h | BLE IRAM 优化 + WiFi NTP 校时 |
| 4-7h | BLE notify 调试（notify 返回 void 是根因） |
| 7-8h | BLE 双向通信打通 + 中文 UTF-8 支持 |
| 8-10h | 文档更新 + CLAUDE.md 维护 |

## BLE notify 根因

`ESP32 BLE Arduino` 库 v2.0.0 的 `BLECharacteristic::notify()` **返回 void，不是 bool**。之前的代码 `bool ok = pTxChar->notify()` 编译通过（用了缓存目标文件），但运行时导致连接断开。改为直接调用 `pTxChar->notify()` 后一切正常。

## 最终项目状态

| 模块 | 状态 | 备注 |
|------|------|------|
| 屏幕 | ✅ LVGL 8.4.0 | 数字+模拟+传感器 3页 |
| 触控 | ✅ CST816D | 左/右滑动翻页 |
| RTC | ✅ PCF85063 | NTP 自动校时 |
| IMU | ✅ QMI8658 | 计步算法优化版 |
| PMU | ✅ AXP2101 | 电池百分比 |
| WiFi | ✅ 自动连接 | iQOO 热点 |
| BLE | ✅ 双向通信 | Notify + Write |
| 中文 | ✅ UTF-8 行缓冲 | 串口→BLE |

## 资源

| 指标 | 数值 |
|------|------|
| RAM | 148KB / 320KB (46%) |
| Flash | 1.6MB / 3.3MB (49%) |
| 提交数 | 12 commits（今日 8 个） |

## 已知问题

1. PSRAM 初始化失败（`PSRAM ID read error: 0x00ffffff`），`-DBOARD_HAS_PSRAM` 已注释
2. 自定义板 JSON `ESP32-S3-R8-OPI` 导致 USBSerial 未定义，搁置
3. 无手机 App，只支持 nRF Connect 调试
4. 低功耗模式未开始
