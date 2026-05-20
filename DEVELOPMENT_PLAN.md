# SmartBracelet 智能手表开发计划报告

> 基于 Waveshare ESP32-S3-Touch-LCD-1.83 的边缘 AI 智能手表

---

## 目录

1. [项目概述](#1-项目概述)
2. [硬件平台分析](#2-硬件平台分析)
3. [开源方案调研](#3-开源方案调研)
4. [边缘 AI 技术路线](#4-边缘-ai-技术路线)
5. [手表功能规划](#5-手表功能规划)
6. [软件架构设计](#6-软件架构设计)
7. [分阶段开发计划](#7-分阶段开发计划)
8. [工具链与环境](#8-工具链与环境)
9. [风险评估](#9-风险评估)

---

## 1. 项目概述

### 1.1 目标

基于 Waveshare ESP32-S3-Touch-LCD-1.83 开发板，打造一款具备**边缘 AI 能力**的智能手表，涵盖健康监测、语音交互、通知同步等核心手表功能。

### 1.2 核心差异化

| 维度    | 定位                                              |
| ----- | ----------------------------------------------- |
| 硬件平台  | ESP32-S3 (240MHz 双核, 16MB Flash, 8MB OPI PSRAM) |
| 显示方案  | ST7789 240×284 触摸屏                              |
| AI 路线 | **端侧推理为主** (TFLite Micro / ESP-DL)，云端 LLM 为辅    |
| 开发环境  | PlatformIO + Arduino 框架                         |
| 开源策略  | 基于 LVGL 自研，参考社区最佳实践                             |

---

## 2. 硬件平台分析

### 2.1 Waveshare ESP32-S3-Touch-LCD-1.83 规格

| 组件         | 型号/规格                            | 备注                    |
| ---------- | -------------------------------- | --------------------- |
| **主控**     | ESP32-S3R8, Xtensa LX7 双核 240MHz | 支持向量指令集加速 AI          |
| **Flash**  | 16MB                             | 充足的存储空间               |
| **PSRAM**  | 8MB OPI                          | 大内存利于 AI 模型 + LVGL 缓冲 |
| **屏幕**     | ST7789, 240×284, SPI             | 需偏移量 (0,20,0,0)       |
| **触摸**     | CST816D (I2C, 地址 0x15)           | 电容触摸，CST816S 库兼容驱动    |
| **PMU**    | AXP2101                          | 电源管理、电池充电             |
| **IMU**    | QMI8658 (I2C)                    | 6 轴加速度+陀螺仪            |
| **I2C 接口** | SDA=15, SCL=14                   | 传感器总线                 |
| **USB**    | USB-C, HWCDC (USBSerial)         | 烧录+调试+供电              |

### 2.2 板上引脚分配

| 信号       | 引脚     | 功能      |
| -------- | ------ | ------- |
| LCD_DC   | GPIO4  | 数据/命令选择 |
| LCD_CS   | GPIO5  | SPI 片选  |
| LCD_SCK  | GPIO6  | SPI 时钟  |
| LCD_MOSI | GPIO7  | SPI 数据  |
| LCD_RST  | GPIO38 | 屏幕复位    |
| LCD_BL   | GPIO40 | 背光控制    |
| TP_RST   | GPIO39 | 触摸复位    |
| TP_INT   | GPIO13 | 触摸中断    |
| IIC_SDA  | GPIO15 | I2C 数据  |
| IIC_SCL  | GPIO14 | I2C 时钟  |

### 2.3 硬件限制

- **有** PCF85063 RTC 芯片（I2C 0x51，板上已集成）
- 无音频编解码器（需要外接 I2S MEMS Mic，板上 ES8311 音频编解码器待确认接线）
- 无心率/血氧传感器

## 3. 开源方案调研

### 3.1 同类项目对比

| 项目                           | 平台          | 框架             | UI       | AI/ML            | 特点                              |
| ---------------------------- | ----------- | -------------- | -------- | ---------------- | ------------------------------- |
| **OpenTimeWatch-OS**         | ESP32-S3    | Arduino        | LVGL     | -                | 成熟 OS，多表盘，应用框架                  |
| **Hacktor-Watch 2.0**        | ESP32-S3    | Arduino/Zephyr | LVGL     | 标注支持 Tiny AI     | 开源硬件，32MB Flash，传感器丰富           |
| **TinyWATCH S3**             | ESP32-S3    | PlatformIO     | -        | -                | Unexpected Maker，BMI270+MMC5603 |
| **xiaozhi-esp32 (wristgem)** | ESP32-S3    | ESP-IDF        | LCD      | **ESP-SR + LLM** | 中文 AI 对话，声纹，SenseVoice          |
| **fbiego/esp32-lvgl-watch**  | ESP32/C3/S3 | Arduino        | **LVGL** | -                | BLE 通知，表盘商店，**支持 1.83"**        |
| **survivorhao/esp32s3watch** | ESP32-S3    | **ESP-IDF**    | LVGL     | -                | 功能最全（WiFi/BLE/相机/SD/睡眠）         |
| **esp32pico_watch**          | ESP32-PICO  | Arduino        | TFT      | **MFCC+CNN**     | 端侧语音识别，95% 准确率                  |
| **ZephyrWatch**              | ESP32-S3    | **Zephyr**     | LVGL     | -                | **支持 1.28"**，BLE 校时             |
| **S3Watch**                  | ESP32-S3    | ESP-IDF        | LVGL     | -                | **针对 Waveshare AMOLED 2.06**    |

### 3.2 可借鉴的技术方案

**fbiego/esp32-lvgl-watch**（最值得参考）

- 直接**支持我们的 1.83" 屏幕**
- LVGL 9 的成熟 UI 框架
- BLE 通知同步、表盘、天气、音乐控制
- 安装自定义表盘机制

**xiaozhi-esp32（AI 功能参考）**

- ESP-SR 离线语音唤醒
- 云端 LLM 流式对话
- SenseVoice 多语言识别
- 声纹识别

**esp32pico_watch（边缘 AI 参考）**

- MFCC + CNN 端侧部署
- TensorFlow Lite Micro 实际产品化
- 整机待机 72h+

---

## 4. 边缘 AI 技术路线

### 4.1 ESP32-S3 AI 加速能力

ESP32-S3 提供 **向量指令集**（PIE），专为 AI 推理加速设计：

- 单周期 MAC 操作
- 8/16-bit 定点加速
- SIMD 向量处理
- 配合 **ESP-NN** 库获得接近 DSP 的推理性能

### 4.2 可选技术方案对比

| 方案               | 类型                | 复杂度  | 适用场景                | 工具链                        |
| ---------------- | ----------------- | ---- | ------------------- | -------------------------- |
| **TFLite Micro** | 通用推理框架            | ⭐⭐⭐  | 活动识别、分类、回归          | TFLiteMicro_ArduinoESP32S3 |
| **ESP-DL**       | Espressif 官方 DL 库 | ⭐⭐⭐  | 人脸检测、图像分类           | ESP-IDF                    |
| **ESP-SR**       | Espressif 语音识别    | ⭐⭐   | 离线语音唤醒、命令词          | ESP-IDF / Arduino          |
| **ESP-NN**       | 优化的 NN 核函数        | ⭐    | 加速 TFLite/ESP-DL 底层 | 库依赖                        |
| **MFCC+CNN**     | 自建轻量流水线           | ⭐⭐⭐  | 自定义分类（手势/声音）        | Python 训练 + C 部署           |
| **Edge Impulse** | 端到端 ML 平台         | ⭐⭐   | 快速原型，无需写 Python     | Edge Impulse CLI           |
| **云端 LLM 混合**    | 端侧唤醒+云侧推理         | ⭐⭐⭐⭐ | 语音助手、AI 问答          | HTTP/WebSocket 客户端         |

### 4.3 推荐路线

```
第一阶段（基础）    第二阶段（增强）        第三阶段（AI 核心）
┌──────────────┐   ┌──────────────┐       ┌──────────────────┐
│ TFLite Micro  │ → │ ESP-SR 唤醒  │   →   │ 混合 AI 架构     │
│ 活动识别      │   │ MFCC+CNN    │       │ 端侧+云端 LLM    │
│ 计步算法      │   │ 手势控制    │       │ 个性化模型       │
└──────────────┘   └──────────────┘       └──────────────────┘
```

### 4.4 模型训练工具链

**本地已具备：**

- **PyTorch 2.11 + CUDA** — 训练深度学习模型
- **scikit-learn / pandas** — 数据处理
- **Transformers** — 预训练模型

**需补充：**

- `pip install tensorflow` — TFLite 模型导出
- `pip install onnx onnxruntime` — 模型转换
- Edge Impulse CLI（可选）

### 4.5 适用场景

| AI 功能        | 方案                    | 模型大小   | 内存需求   | 优先级 |
| ------------ | --------------------- | ------ | ------ | --- |
| 计步/活动识别      | TFLite Micro + 轻量 CNN | ~50KB  | ~100KB | P0  |
| 抬手亮屏         | IMU + 简单阈值            | -      | -      | P0  |
| 手势控制 (甩腕/翻腕) | MFCC+CNN 或阈值          | ~30KB  | ~80KB  | P1  |
| 离线语音唤醒       | ESP-SR                | ~200KB | ~300KB | P1  |
| 语音命令识别       | TFLite Micro + 音频 CNN | ~100KB | ~200KB | P2  |
| 异常跌倒检测       | TFLite Micro + 1D CNN | ~30KB  | ~80KB  | P2  |
| AI 语音助手      | 云端 LLM (端侧唤醒)         | -      | -      | P2  |

---

## 5. 手表功能规划

### 5.1 功能矩阵

| 功能       | 类别  | 优先级 | 说明                  |
| -------- | --- | --- | ------------------- |
| 时间显示     | 核心  | P0  | WiFi/BLE 校时，多表盘     |
| 触摸交互     | 核心  | P0  | LVGL 滑动/点击          |
| 背光控制     | 核心  | P0  | 亮度调节，自动息屏           |
| 低功耗休眠    | 核心  | P0  | Deep sleep + RTC 唤醒 |
| 计步器      | 健康  | P0  | IMU + TFLite Micro  |
| 通知同步     | 通信  | P1  | BLE GATT            |
| 天气显示     | 工具  | P1  | WiFi 获取             |
| 抬手亮屏     | 交互  | P1  | IMU 检测              |
| 秒表/倒计时   | 工具  | P1  | 基础功能                |
| BLE 音乐控制 | 通信  | P1  | HID 协议              |
| OTA 升级   | 系统  | P1  | HTTP/BLE            |
| 心率估算     | 健康  | P2  | PTT 算法（非 PPG）       |
| 跌倒检测     | 安全  | P2  | AI 模型               |
| 语音助手     | AI  | P2  | 唤醒+云端 LLM           |
| 查找手机     | 工具  | P2  | BLE                 |
| 自定义表盘    | UI  | P2  | LVGL 动态加载           |

### 5.2 低功耗策略

| 模式  | 状态               | 电流估算   | 唤醒方式   |
| --- | ---------------- | ------ | ------ |
| 活跃  | CPU 240MHz + 屏幕亮 | ~80mA  | -      |
| 空闲  | CPU 80MHz, 屏幕灭   | ~30mA  | 触摸/抬手  |
| 浅睡  | 保留 PSRAM         | ~10mA  | 触摸/IMU |
| 深睡  | 仅 RTC + ULP      | ~2.5mA | 按键/定时  |

目标：正常使用续航 **24h+**，待机 **72h+**

---

## 6. 软件架构设计

### 6.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层                                   │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐   │
│  │ 表盘  │ │ 通知  │ │ 健康  │ │ 天气  │ │ 设置  │ │ 应用  │   │
│  │      │ │      │ │      │ │      │ │      │ │ ...  │   │
│  └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──────┘   │
├─────┼────────┼────────┼────────┼────────┼───────────────────┤
│     │        │        │        │        │                   │
│  ┌──┴────────┴────────┴────────┴────────┴────────────────┐ │
│  │                    LVGL UI 层                           │ │
│  │           (触摸事件分发 + 页面管理 + 动画)               │ │
│  └──────────────────────────┬─────────────────────────────┘ │
│                             │                                │
│  ┌──────────────────────────┴─────────────────────────────┐ │
│  │                  服务层                                  │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │ │
│  │  │ BLE 服务 │ │ WiFi 管理 │ │ IMU  处理│ │ AI 推理  │  │ │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │ │
│  │  │ 电源管理  │ │ 文件系统  │ │ OTA      │ │ RTC 服务 │  │ │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │ │
│  └──────────────────────────┬─────────────────────────────┘ │
│                             │                                │
│  ┌──────────────────────────┴─────────────────────────────┐ │
│  │                  硬件抽象层 (HAL)                        │ │
│  │  ST7789 | CST816S | QMI8658 | AXP2101 | BLE | Flash   │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 关键库选择

| 层      | 方案                        | 原因             |
| ------ | ------------------------- | -------------- |
| UI 框架  | **LVGL 8.4.0**            | 已安装，社区成熟       |
| 显示驱动   | **Arduino_GFX (ST7789)**  | 已验证可用          |
| 触摸驱动   | **CST816S (fbiego)**      | 兼容 CST816D，已验证 |
| IMU 驱动 | **SensorLib**             | 已安装            |
| BLE    | **Arduino BLE / NimBLE**  | Arduino 原生支持   |
| AI 推理  | **TFLite Micro / ESP-NN** | 轻量，支持 S3 加速    |
| 电源管理   | **自研 AXP2101 驱动**         | 片上 PMU         |

### 6.3 目录结构

```
SmartBracelet/
├── src/
│   ├── main.cpp
│   ├── app/           # 应用模块
│   │   ├── watchface/ # 表盘
│   │   ├── health/    # 健康
│   │   ├── weather/   # 天气
│   │   ├── settings/  # 设置
│   │   └── notify/    # 通知
│   ├── service/       # 后台服务
│   │   ├── ble.cpp/h
│   │   ├── wifi.cpp/h
│   │   ├── power.cpp/h
│   │   └── ota.cpp/h
│   ├── ai/            # AI 推理
│   │   ├── models/    # TFLite 模型
│   │   ├── imu_classifier.cpp/h
│   │   └── voice_wakeup.cpp/h
│   ├── ui/            # LVGL UI
│   │   ├── pages/
│   │   ├── components/
│   │   └── styles/
│   └── hal/           # 硬件抽象
│       ├── display.cpp/h
│       ├── touch.cpp/h
│       ├── imu.cpp/h
│       ├── pmu.cpp/h
│       └── battery.cpp/h
├── include/
│   ├── pin_config.h
│   └── lv_conf.h
├── lib/               # 本地库
│   └── GFX_Library_for_Arduino/
├── models/            # 训练脚本 + 模型
│   ├── train_activity.py
│   ├── convert_to_tflm.py
│   └── models/
├── platformio.ini
└── boards/            # 自定义板级定义
    └── ESP32-S3-R8-OPI.json
```

---

## 7. 分阶段开发计划

### 第一阶段：基础平台（4 周）

**目标**：稳定的显示 + 触摸 + 串口，构建最小可运行系统

| 周次  | 任务                          | 交付物            |
| --- | --------------------------- | -------------- |
| 1   | 搭建 LVGL + Arduino_GFX 显示流水线 | 屏幕显示 LVGL demo |
| 2   | 集成 CST816S 触摸 + LVGL 输入驱动   | 触摸滑动、点击响应      |
| 3   | AXP2101 PMU 驱动（充电、电量检测）     | 电量显示、充电管理      |
| 4   | BLE 连接 + WiFi 校时            | 手机连接、时间同步      |

**已完成的准备工作：**

- ✅ 板级引脚定义 (`pin_config.h`) — 已按官方仓库确认
- ✅ 屏幕初始化（偏移 0,20,0,0）已验证
- ✅ USBSerial 串口正常（带 3 秒超时，避免 USB 枚举卡死）
- ✅ LVGL 8.4.0 依赖已安装 + 显示驱动正常
- ✅ LVGL 显示缓冲区配置（240×284/10，RGB565，SWAP=0）
- ✅ CST816D 触控驱动（使用 fbiego/CST816S 库，兼容驱动）
- ✅ 触控引脚移到 I2C 总线 15/14（与原 USB 引脚 19/20 解冲突）
- ✅ LVGL 指针输入驱动已注册

### 第二阶段：核心手表功能（6 周）

**目标**：可日常使用的基础手表

| 周次  | 任务                    | 交付物     |
| --- | --------------------- | ------- |
| 5   | 多表盘框架 + 数字/模拟表盘       | 2-3 款表盘 |
| 6   | LVGL 页面管理和滑动导航        | 页面切换动画  |
| 7   | QMI8658 IMU 驱动 + 计步算法 | 步数显示    |
| 8   | BLE 通知同步（GATT）        | 来电/消息推送 |
| 9   | 天气 + 日历功能             | 联网信息显示  |
| 10  | 低功耗管理（Deep sleep）     | 续航优化    |

### 第三阶段：边缘 AI（4 周）

**目标**：端侧 AI 能力落地

| 周次  | 任务                          | 交付物       |
| --- | --------------------------- | --------- |
| 11  | TFLite Micro 集成 + ESP-NN 加速 | 可运行的推理管线  |
| 12  | 活动识别模型（行走/跑步/静止）            | 自动活动检测    |
| 13  | ESP-SR 离线语音唤醒               | "嘿 手表" 唤醒 |
| 14  | 异常检测（跌倒检测）                  | 安全告警      |

**Python 训练前置任务（与固件开发并行）：**

- 数据采集脚本（IMU 数据 -> CSV）
- PyTorch 模型训练 -> TFLite 转换
- 模型量化（INT8）+ 剪枝

### 第四阶段：增强功能（4 周）

**目标**：完整智能体验

| 周次  | 任务               | 交付物    |
| --- | ---------------- | ------ |
| 15  | OTA 升级（HTTP/BLE） | 无线固件更新 |
| 16  | 抬手亮屏 + 手势控制      | 翻腕切页   |
| 17  | BLE 音乐控制（HID）    | 切歌/音量  |
| 18  | 综合调试 + 功耗优化      | 稳定发布   |

### 第五阶段：AI 增强（持续）

**目标**：打造差异化 AI 体验

| 任务        | 说明                           |
| --------- | ---------------------------- |
| 混合 AI 架构  | 端侧唤醒 + 云端 LLM 对话（参考 xiaozhi） |
| 个性化 AI 模型 | 基于用户数据微调活动识别模型               |
| 声纹识别      | 区分用户身份的语音 ID                 |
| 端侧 NLP    | 轻量文本分类/意图识别                  |

---

## 8. 工具链与环境

### 8.1 开发环境

| 工具                     | 用途          | 状态                        |
| ---------------------- | ----------- | ------------------------- |
| **PlatformIO**         | 嵌入式构建       | ✅ 已安装 (espressif32@6.9.0) |
| **Visual Studio Code** | IDE         | ✅ 已安装                     |
| **Python 3.12**        | 模型训练 + 工具脚本 | ✅ 已安装                     |

### 8.2 模型训练环境

| 工具                          | 用途          | 状态    |
| --------------------------- | ----------- | ----- |
| **PyTorch 2.11 + CUDA 128** | 模型训练        | ✅ 已安装 |
| **scikit-learn 1.8**        | 数据处理        | ✅ 已安装 |
| **Transformers 5.8**        | 预训练模型       | ✅ 已安装 |
| **TensorFlow**              | TFLite 模型导出 | ❌ 需安装 |
| **ONNX Runtime**            | 模型转换        | ❌ 需安装 |

### 8.3 需安装的工具

```bash
pip install tensorflow
pip install onnx onnxruntime
```

### 8.4 烧录方式

推荐使用 esptool.py 直接烧录（`pio run --target upload` 在 921600 波特率下不稳定）：

```powershell
python "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" `
    --chip esp32s3 --port COM9 --baud 115200 `
    --before default_reset --after hard_reset write_flash -z `
    --flash_mode qio --flash_freq 80m --flash_size 16MB `
    0x0 ".pio/build/esp32s3/bootloader.bin" `
    0x8000 ".pio/build/esp32s3/partitions.bin" `
    0x10000 ".pio/build/esp32s3/firmware.bin"
```

**注意**：eFuse 锁定 QIO 模式，必须用 `--flash_mode qio`；波特率用 115200 避免 `Packet content transfer stopped` 错误。

---

## 9. 风险评估

| 风险                              | 影响  | 概率  | 缓解措施                                                                 |
| ------------------------------- | --- | --- | -------------------------------------------------------------------- |
| PSRAM 初始化失败（boot loop）          | 高   | 中   | 用 `-DBOARD_HAS_PSRAM` + 标准板型，不要用自定义 `ESP32-S3-R8-OPI`（USBSerial 未定义） |
| IRAM 溢出（库体积大 → TG0WDT 复位）       | 高   | 中   | 移除不用的 lib_deps（SensorLib、Arduino_DriveBus），保持固件 <600KB Flash         |
| LVGL 大内存消耗触发 OOM                | 高   | 中   | 控制显示缓冲区大小（当前 240×284/10 = 13.6KB），必要时用 PSRAM                         |
| TFLite Micro 模型无法在 8MB PSRAM 加载 | 中   | 低   | 模型量化 INT8，剪枝优化                                                       |
| 续航不足                            | 高   | 中   | 分阶段优化：空闲降频 → 浅睡 → Deep sleep                                         |
| BLE 协议栈与 WiFi 共存冲突              | 中   | 低   | 分时复用，或 NimBLE 优化                                                     |
| 屏幕偏移/旋转不正确                      | 中   | 低   | 已验证偏移 (0,20,0,0)                                                     |
| USB 串口静默崩溃                      | 高   | 低   | 全局 `new` 放入 `setup()`；USBSerial 加 3 秒超时；不自定义板 JSON                   |
| RTS 复位不可靠                       | 中   | 高   | 上传后手动拔插 USB 冷启动                                                      |
| 无 TFLite（需安装）影响 AI 进度           | 中   | 高   | 优先用 Python 完成模型训练，最后安装 TensorFlow                                    |

---

## 附录

### A. 关键参考项目

| 项目                         | 链接                                                        | 参考价值                             |
| -------------------------- | --------------------------------------------------------- | -------------------------------- |
| fbiego/esp32-lvgl-watch    | https://github.com/fbiego/esp32-c3-mini                   | **直接支持 1.83" 屏幕，LVGL UI 框架最佳参考** |
| xiaozhi-esp32 (wristgem)   | https://github.com/dotnfc/xiaozhi-esp32/tree/wristgem     | AI 语音助手架构                        |
| TFLiteMicro_ArduinoESP32S3 | https://github.com/j-siderius/TFLiteMicro_ArduinoESP32S3  | TFLite Micro + ESP-NN 集成         |
| OpenTimeWatch-OS           | https://github.com/OpenTimeWatch-Project/OpenTimeWatch-OS | Arduino 手表 OS 参考                 |
| esp32pico_watch            | https://github.com/dht3218/esp32pico_watch                | MFCC+CNN 端侧语音                    |
| ZephyrWatch                | https://github.com/electricalgorithm/zephyr-watch         | 同款板 LCD-1.28                     |

### B. 已调试经验

见 `DEBUG_REPORT.md` — 包含以下 5 次调试记录：

1. **白屏/启动循环** — USBSerial 重复定义、全局 new 崩溃
2. **USB 插拔后 flash 损坏** — 全片擦除 + esptool 手动三段式烧录
3. **LVGL 集成** — 显示缓冲区、颜色字节序 (SWAP=0)、配置路径
4. **LVGL 验证 + 触摸集成** — RST 复位、触摸引脚 19/20 → 15/14 迁移
5. **Boot Loop 修复** — Arduino_DriveBus → CST816S 回退、精简依赖
