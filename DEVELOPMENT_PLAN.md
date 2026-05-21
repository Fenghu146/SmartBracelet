# SmartBracelet 智能手表开发计划报告

> 基于 Waveshare ESP32-S3-Touch-LCD-1.83 的边缘 AI 智能手表

---

## 当前进度

| 里程碑                                     | 状态         |
| --------------------------------------- | ---------- |
| 显示驱动 (ST7789 + Arduino_GFX)             | ✅ 已验证      |
| LVGL 8.4.0 集成 (显示 + 颜色校准)               | ✅ 已验证      |
| 触控驱动 (CST816D via CST816S 库, IIC 15/14) | ✅ 滑动/手势/点击 |
| RTC (PCF85063) 时间读取 + 写入                | ✅          |
| IMU (QMI8658) 加速度 + 陀螺仪                 | ✅          |
| PMU (AXP2101) 电量读取 + 电源配置               | ✅          |
| 数字表盘 UI (时间/日期/电池/步数)                   | ✅          |
| 多页面导航 (手势左/右滑动)                         | ✅          |
| 计步器 (IMU 峰值检测)                          | ✅          |
| BLE 通知同步 + 双向通信 + 中文 UTF-8              | ✅          |
| WiFi NTP 校时                             | ✅          |
| 低功耗管理 (深度睡眠)                            | ✅          |
| 抬手亮屏 (IMU 重力角度检测)                       | ✅          |
| 通知 UI (第 4 页 + LV_LABEL_LONG_WRAP)      | ✅          |
| 模拟表盘                                    | ✅          |
| 电池电压/百分比直接 ADC 读取（绕过检测位）                | ✅          |
| USB 充电保活（插 USB 不深睡）                     | ✅          |
| 边缘 AI (TFLite Micro)                    | ❌ 远期目标     |

**当前阶段**：基础通信 + 续航 + UI 全部完成，电池保护板锁死为已知硬件问题。下一步可进入 OTA/增强功能/边缘 AI。

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

| 功能       | 类别  | 优先级 | 状态  | 说明                              |
| -------- | --- | --- | --- | ------------------------------- |
| 时间显示     | 核心  | P0  | ✅   | LVGL 数字表盘 + 模拟表盘 + RTC 校时       |
| 触摸交互     | 核心  | P0  | ✅   | LVGL 滑动/点击/手势翻页                 |
| 背光控制     | 核心  | P0  | ✅   | 自动息屏 10s + 触摸/抬腕唤醒              |
| 低功耗休眠    | 核心  | P0  | ✅   | Deep sleep 30s + 触摸/RTC 60s 唤醒  |
| 计步器      | 健康  | P0  | ✅   | IMU 低通滤波 + 自适应基线峰值检测            |
| 通知同步     | 通信  | P0  | ✅   | BLE GATT + UTF-8 中文双向           |
| WiFi 校时  | 通信  | P0  | ✅   | WiFi 自动连接 + NTP → RTC           |
| 抬手亮屏     | 交互  | P1  | ✅   | IMU 重力角度 vs 休止基线                |
| 模拟表盘     | UI  | P1  | ✅   | LVGL line 时针/分针/秒针 + 刻度         |
| 电池电量     | 系统  | P0  | ✅   | 原始 ADC 读取 (绕过 isBatteryConnect) |
| USB 充电保活 | 系统  | P1  | ✅   | 插 USB 跳过深睡，保持串口                 |
| 天气显示     | 工具  | P1  | ❌   | WiFi 获取                         |
| 秒表/倒计时   | 工具  | P1  | ❌   | 基础功能                            |
| BLE 音乐控制 | 通信  | P1  | ❌   | HID 协议                          |
| OTA 升级   | 系统  | P1  | ❌   | HTTP/BLE                        |
| 心率估算     | 健康  | P2  | ❌   | PTT 算法（非 PPG）                   |
| 跌倒检测     | 安全  | P2  | ❌   | AI 模型                           |
| 语音助手     | AI  | P2  | ❌   | 唤醒+云端 LLM                       |
| 查找手机     | 工具  | P2  | ❌   | BLE                             |
| 自定义表盘    | UI  | P2  | ❌   | LVGL 动态加载                       |

### 5.2 低功耗策略

| 模式  | 状态               | 电流估算   | 唤醒方式              |
| --- | ---------------- | ------ | ----------------- |
| 活跃  | CPU 240MHz + 屏幕亮 | ~80mA  | -                 |
| 息屏  | CPU 240MHz, 屏幕灭  | ~30mA  | 触摸/抬腕             |
| 深睡  | 仅 RTC + ULP      | ~2.5mA | 触摸中断 / RTC 60s 定时 |

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

| 层      | 方案                                | 原因             |
| ------ | --------------------------------- | -------------- |
| UI 框架  | **LVGL 8.4.0**                    | 已安装，社区成熟       |
| 显示驱动   | **Arduino_GFX (ST7789)**          | 已验证可用          |
| 触控驱动   | **CST816S (fbiego)**              | 兼容 CST816D，已验证 |
| RTC 驱动 | **SensorLib-Waveshare**           | 官方 PCF85063 驱动 |
| IMU 驱动 | **SensorLib-Waveshare**           | 官方 QMI8658 驱动  |
| PMU 驱动 | **XPowersLib**                    | AXP2101 完整支持   |
| BLE    | **ESP32 BLE Arduino** (Bluedroid) | 原生集成，无需额外库     |
| WiFi   | **Arduino WiFi** (内置)             | ESP32-S3 原生    |
| AI 推理  | **TFLite Micro / ESP-NN**         | 轻量，支持 S3 加速    |

### 6.3 目录结构

```
SmartBracelet/
├── src/
│   ├── main.cpp              # 入口 + 当前全部业务逻辑（待拆分）
│   ├── lv_port_disp.cpp/h    # LVGL 显示端口
│   ├── lv_port_indev.cpp/h   # LVGL 触控端口
│   ├── pin_config.h
│   ├── app/                  # 应用模块（待创建）
│   ├── service/              # 后台服务（待创建）
│   │   ├── ble.cpp/h
│   │   ├── wifi.cpp/h
│   │   └── power.cpp/h
│   ├── ui/                   # LVGL UI 组件（待创建）
│   │   ├── pages/
│   │   └── components/
│   └── hal/                  # 硬件抽象（待创建）
│       ├── display.cpp/h
│       ├── touch.cpp/h
│       ├── imu.cpp/h
│       └── pmu.cpp/h
├── include/
│   ├── pin_config.h
│   └── lv_conf.h
├── lib/                      # 本地库
│   ├── GFX_Library_for_Arduino/
│   ├── SensorLib-Waveshare/  # RTC + IMU 驱动
│   └── XPowersLib/           # AXP2101 PMU 驱动
├── boards/                   # 自定义板级定义
│   └── ESP32-S3-R8-OPI.json
├── platformio.ini
├── DEVELOPMENT_PLAN.md
├── DEBUG_REPORT.md
└── CLAUDE.md
```

---

## 7. 分阶段开发计划

### 已完成：基础平台（Phase 1 — 核心）

**原计划 4 周，实际提前完成。**

| 模块                       | 状态  | 说明                                       |
| ------------------------ | --- | ---------------------------------------- |
| LVGL + Arduino_GFX 显示流水线 | ✅   | ST7789 240×284, 偏移 (0,20), RGB565 SWAP=0 |
| CST816S 触摸 + LVGL 输入驱动   | ✅   | IIC 15/14, 手势左/右滑动翻页                     |
| AXP2101 PMU 驱动           | ✅   | 电量读取, 电源轨配置, 充电管理                        |
| RTC PCF85063             | ✅   | 时间读写, WiFi 校时已完成                         |
| IMU QMI8658              | ✅   | 加速度+陀螺仪, 计步算法 + 抬手亮屏                     |
| LVGL 表盘 UI               | ✅   | 数字/模拟双表盘, 4 页导航, 传感器页, 通知页               |
| BLE 通知 + 双向通信            | ✅   | GATT 自定义 Profile, UTF-8 中文               |
| WiFi NTP 校时              | ✅   | 自动连接 + NTP → RTC                         |
| Deep sleep 低功耗           | ✅   | 30s 深睡 + 触摸/定时唤醒 + USB 充电保活              |

---

### 已完成：通信 + 续航 + 交互（Phase 2）

**目标**：补齐 BLE 通知同步 + WiFi 校时 + 低功耗深度睡眠，达到可日常佩戴的基础体验。

| 任务               | 状态  | 说明                          |
| ---------------- | --- | --------------------------- |
| BLE 基础服务         | ✅   | GATT Server, 双向通信, UTF-8 中文 |
| WiFi 校时 + 通知通道   | ✅   | WiFi 自动连接 + NTP → RTC       |
| 通知 UI + 抬手亮屏     | ✅   | 通知第 4 页 + IMU 重力角度检测        |
| Deep Sleep 低功耗   | ✅   | 30s 深睡 + 触摸/RTC 唤醒 + USB 保活 |
| 电池 ADC 读取（绕过检测位） | ✅   | 直接读取 0x34-0x35/A4 寄存器       |

**实际技术选型差异：**

| 决策      | 计划          | 实际                                | 原因                  |
| ------- | ----------- | --------------------------------- | ------------------- |
| BLE 协议栈 | NimBLE      | **ESP32 BLE Arduino** (Bluedroid) | 简化集成，Arduino API 易用 |
| 配网方式    | BLE 配网      | **WiFi 自动连接**                     | 无需 App，SSID 硬编码     |
| BLE 通知  | 自定义 GATT 标准 | 自定义 GATT                          | 与 nRF Connect 兼容    |

---

### 第二阶段：增强功能（Phase 3 — 4 周）

**目标**：完善手表体验，增加实用功能。

| 周次  | 任务                                  | 交付物           |
| --- | ----------------------------------- | ------------- |
| 1   | **OTA 升级**：HTTP 固件下载 + 差分更新         | 无线固件更新        |
| 2   | **天气 + 日历**：WiFi 获取天气 API，LVGL 天气页面 | 实时天气显示        |
| 3   | **秒表/倒计时 + BLE 音乐控制**               | 工具功能 + HID 遥控 |
| 4   | **综合调试 + 续航优化**：各模式电流测量，代码尺寸优化      | 稳定版发布         |

---

### 第三阶段：边缘 AI（Phase 4 — 4 周）

**目标**：端侧 AI 能力落地。

| 周次  | 任务                          | 交付物       |
| --- | --------------------------- | --------- |
| 1   | TFLite Micro 集成 + ESP-NN 加速 | 可运行的推理管线  |
| 2   | 活动识别模型（行走/跑步/静止）            | 自动活动检测    |
| 3   | ESP-SR 离线语音唤醒               | "嘿 手表" 唤醒 |
| 4   | 异常检测（跌倒检测）                  | 安全告警      |

**Python 训练前置任务（与固件开发并行）：**

- 数据采集脚本（IMU 数据 -> CSV）
- PyTorch 模型训练 -> TFLite 转换
- 模型量化（INT8）+ 剪枝

---

### 第四阶段：AI 增强（Phase 5 — 持续）

**目标**：打造差异化 AI 体验。

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

| 风险                       | 影响  | 概率  | 缓解措施                                                            |
| ------------------------ | --- | --- | --------------------------------------------------------------- |
| PSRAM 初始化失败（boot loop）   | 高   | 低   | 已解决：用标准板型 `esp32-s3-devkitc-1` + `-DBOARD_HAS_PSRAM`，不用自定义 JSON |
| IRAM 溢出（TG0WDT 复位）       | 高   | 低   | 已解决：精简 lib_deps（仅 CST816S + lvgl + BLE + WiFi），IRAM 降至安全范围      |
| BLE + WiFi + LVGL 共存内存不足 | 中   | 低   | 当前 RAM 148KB/320KB (45%)，余量 170KB+，BLE 使用 Bluedroid 工作正常        |
| BLE + WiFi 共存射频干扰        | 中   | 低   | 分时复用，连接 WiFi 时 BLE 暂停广播                                         |
| Deep sleep 唤醒失败          | 高   | 低   | 已验证：触摸 INT 唤醒 + RTC 60s 定时唤醒均正常工作                               |
| 续航不足                     | 高   | 低   | Deep sleep 已实现，实测待机功耗待测量                                        |
| 电池保护板锁死（过放）              | 中   | 中   | 已知硬件问题：保护板触发后需断开重接或换电池，软件已做兼容（直接读 ADC）                          |
| USB 串口静默崩溃               | 高   | 低   | 已验证：全局 `new` 放入 `setup()`；USBSerial 加 3 秒超时；不自定义板 JSON          |
| RTS 复位不可靠                | 中   | 高   | 上传后手动拔插 USB 冷启动                                                 |
| 无 TFLite（需安装）影响 AI 进度    | 中   | 高   | Phase 4 才开始，有充足时间准备                                             |

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

见 `DEBUG_REPORT.md` — 包含以下 9 次调试记录：

1. **白屏/启动循环** — USBSerial 重复定义、全局 new 崩溃
2. **USB 插拔后 flash 损坏** — 全片擦除 + esptool 手动三段式烧录
3. **LVGL 集成** — 显示缓冲区、颜色字节序 (SWAP=0)、配置路径
4. **LVGL 验证 + 触摸集成** — RST 复位、触摸引脚 19/20 → 15/14 迁移
5. **Boot Loop 修复** — Arduino_DriveBus → CST816S 回退、精简依赖
6. **传感器集成 + 表盘 UI** — RTC/IMU/PMU 全通、LVGL 多页面表盘
7. **BLE 双向通信 + 中文 UTF-8** — NimBLE → ESP32 BLE Arduino 迁移
8. **抬手亮屏 + 通知页 + Deep sleep** — IMU 重力角度抬腕检测、息屏/深睡状态机
9. **顶部花屏 + 电池诊断 + 充电保活** — ST7789 越界像素清除、ADC 原始读取、USB 跳过深睡
