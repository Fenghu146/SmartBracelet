# SmartBracelet 智能手表开发计划报告

> 基于 Waveshare ESP32-S3-Touch-LCD-1.83 的边缘 AI 智能手表

---

## 当前进度

| 里程碑                                        | 状态                  |
| ------------------------------------------ | ------------------- |
| 显示驱动 (ST7789 + Arduino_GFX)                | ✅ 已验证               |
| LVGL 8.4.0 集成 (显示 + 颜色校准)                  | ✅ 已验证               |
| 触控驱动 (CST816D via CST816S 库, IIC 15/14)    | ✅ 滑动/手势/点击          |
| RTC (PCF85063) 时间读取 + 写入                   | ✅                   |
| IMU (QMI8658) 加速度 + 陀螺仪                    | ✅                   |
| PMU (AXP2101) 电量读取 + 电源配置                  | ✅                   |
| 数字表盘 UI (时间/日期/电池/步数)                      | ✅                   |
| 多页面导航 (手势左/右滑动, 8 页)                       | ✅                   |
| 计步器 (IMU 低通滤波 + 自适应基线峰值检测)                 | ✅                   |
| BLE 通知同步 + 双向通信 + 中文 UTF-8                 | ✅                   |
| WiFi NTP 校时                                | ✅                   |
| 低功耗管理 (深度睡眠 30s + 触摸/RTC 60s 唤醒)           | ✅                   |
| 抬手亮屏 (IMU 重力角度 vs 休止基线检测)                  | ✅                   |
| 通知 UI (LV_LABEL_LONG_WRAP 包裹显示)            | ✅                   |
| 模拟表盘 (LVGL line 时针/分针/秒针 + 12 刻度)          | ✅                   |
| 电池电压/百分比直接 ADC 读取（绕过 isBatteryConnect 检测位） | ✅                   |
| USB 充电保活（插 USB 不深睡，保持串口）                   | ✅                   |
| 秒表/倒计时 (双模式 Start/Stop/Reset/+10s)         | ✅                   |
| 天气显示 (Open-Meteo API, 温度/湿度/条件, 10 分钟刷新)   | ✅                   |
| TF 卡驱动 (SDMMC 1-bit, 文件列表, FAT32)          | ✅                   |
| ES8311 音频编解码器 (I2S, PCA9557 PA_EN, WAV 播放) | ✅                   |
| 音频播放器 UI (Play/Stop/Next, TF 卡 .wav)       | ✅                   |
| 端侧活动识别 AI (随机森林 10 棵树, 行走/跑步/静止)           | ✅ 部署运行              |
| PyTorch + RF 模型训练管线 (数据采集→训练→C 代码导出)       | ✅                   |
| 边缘 AI TFLite Micro 推理                      | 🔄 TFLite → RF 方案   |
| OTA 升级 (HTTP 固件)                           | ❌                   |
| BLE 音乐控制 (HID)                             | ❌                   |
| 综合续航优化 (各模式电流测量)                           | ❌                   |
| 离线语音唤醒 (ESP-SR)                            | ❌ 远期目标              |
| 跌倒检测                                       | ❌ 远期目标              |
| AI 语音助手 (ESP-SR 离线唤醒 + 开源 LLM)             | ❌ 远期目标 (边缘 AI 开源方案) |
| 手机 App (BLE 数据同步 + 通知管理)                   | ❌                   |
| 手机 App (AI 协同推理 + 健康看板)                    | ❌                   |
| 手机 App (OTA 管理)                            | ❌                   |

**当前阶段**：基础平台 + 通信 + 续航 + UI + 工具功能全部完成。音频 (TF + ES8311 + WAV 播放) 和端侧 AI (随机森林活动识别) 已部署运行。下一步可进入 OTA 升级 / 手机 App 开发 (与手表 BLE 联动 + 分布式 AI)。

---

## 1. 项目概述

### 1.1 目标

基于 Waveshare ESP32-S3-Touch-LCD-1.83 开发板，打造一款具备**边缘 AI + 手机协同**能力的智能手表，涵盖健康监测、语音交互、通知同步等核心手表功能。

### 1.2 核心差异化

| 维度     | 定位                                              |
| ------ | ----------------------------------------------- |
| 硬件平台   | ESP32-S3 (240MHz 双核, 16MB Flash, 8MB OPI PSRAM) |
| 显示方案   | ST7789 240×284 触摸屏                              |
| AI 路线  | **手表端侧推理 (RF)** + **手机端推理 (ONNX)** 分布式协作        |
| 手机 App | Flutter 跨平台, BLE 通信, 数据看板, OTA 管理               |
| 开发环境   | PlatformIO + Arduino 框架 + Flutter               |
| 开源策略   | 基于 LVGL 自研，参考社区最佳实践                             |

---

## 2. 硬件平台分析

### 2.1 Waveshare ESP32-S3-Touch-LCD-1.83 规格

| 组件         | 型号/规格                            | 备注                     |
| ---------- | -------------------------------- | ---------------------- |
| **主控**     | ESP32-S3R8, Xtensa LX7 双核 240MHz | 支持向量指令集加速 AI           |
| **Flash**  | 16MB                             | 充足的存储空间                |
| **PSRAM**  | 8MB OPI                          | 大内存利于 LVGL 缓冲          |
| **屏幕**     | ST7789, 240×284, SPI             | 偏移 (0,20,0,0)          |
| **触摸**     | CST816D (I2C, 地址 0x15)           | 电容触摸，CST816S 库兼容驱动     |
| **PMU**    | AXP2101                          | 电源管理、电池充电              |
| **IMU**    | QMI8658 (I2C)                    | 6 轴加速度+陀螺仪             |
| **RTC**    | PCF85063 (I2C 0x51)              | 板上已集成                  |
| **音频**     | ES8311 (I2C 0x18) + PCA9557      | I2S 编解码器, 已驱动支持 WAV 播放 |
| **TF 卡**   | SDMMC 1-bit                      | FAT32, 已驱动             |
| **I2C 接口** | SDA=15, SCL=14                   | 传感器总线                  |
| **USB**    | USB-C, HWCDC (USBSerial)         | 烧录+调试+供电               |

### 2.2 板上引脚分配

| 信号        | 引脚     | 功能      |
| --------- | ------ | ------- |
| LCD_DC    | GPIO4  | 数据/命令选择 |
| LCD_CS    | GPIO5  | SPI 片选  |
| LCD_SCK   | GPIO6  | SPI 时钟  |
| LCD_MOSI  | GPIO7  | SPI 数据  |
| LCD_RST   | GPIO38 | 屏幕复位    |
| LCD_BL    | GPIO40 | 背光控制    |
| TP_RST    | GPIO39 | 触摸复位    |
| TP_INT    | GPIO13 | 触摸中断    |
| IIC_SDA   | GPIO15 | I2C 数据  |
| IIC_SCL   | GPIO14 | I2C 时钟  |
| SDMMC_CLK | GPIO2  | TF 卡时钟  |
| SDMMC_CMD | GPIO1  | TF 卡命令  |
| SDMMC_D0  | GPIO3  | TF 卡数据  |
| I2S_MCK   | GPIO16 | 音频主时钟   |
| I2S_BCK   | GPIO9  | 音频位时钟   |
| I2S_WS    | GPIO45 | 音频声道选择  |
| I2S_DO    | GPIO8  | 音频数据输出  |
| I2S_DI    | GPIO10 | 音频数据输入  |

---

## 3. 开源方案调研

### 3.1 同类项目对比

| 项目                          | 平台          | 框架             | UI       | AI/ML            | 特点                       |
| --------------------------- | ----------- | -------------- | -------- | ---------------- | ------------------------ |
| **OpenTimeWatch-OS**        | ESP32-S3    | Arduino        | LVGL     | -                | 成熟 OS，多表盘，应用框架           |
| **Hacktor-Watch 2.0**       | ESP32-S3    | Arduino/Zephyr | LVGL     | 标注支持 Tiny AI     | 开源硬件，32MB Flash          |
| **TinyWATCH S3**            | ESP32-S3    | PlatformIO     | -        | -                | Unexpected Maker         |
| **xiaozhi-esp32**           | ESP32-S3    | ESP-IDF        | LCD      | **ESP-SR + LLM** | 中文 AI 对话，声纹，SenseVoice   |
| **fbiego/esp32-lvgl-watch** | ESP32/C3/S3 | Arduino        | **LVGL** | -                | BLE 通知，表盘商店，**支持 1.83"** |
| **esp32pico_watch**         | ESP32-PICO  | Arduino        | TFT      | **MFCC+CNN**     | 端侧语音识别                   |
| **RuffCycle (手机 App)**      | Flutter     | flutter_blue   | Material | -                | BLE 骑行码表, 数据同步参考         |
| **Wearable-AI 协同架构**        | 手机+手表       | ONNX + RF      | -        | 分布式推理            | 端侧轻量 + 手机端大模型协作模式        |

---

## 4. 边缘 AI 技术路线

### 4.1 ESP32-S3 AI 加速能力

ESP32-S3 提供 **向量指令集**（PIE），专为 AI 推理加速设计：

- 单周期 MAC 操作, 8/16-bit 定点加速, SIMD 向量处理
- 配合 **ESP-NN** 库获得接近 DSP 的推理性能

### 4.2 分布式 AI 架构 (手表 + 手机)

| 维度    | 手表端 (ESP32-S3)       | 手机端 (Flutter App)     |
| ----- | -------------------- | --------------------- |
| 推理引擎  | 手写 RF / TFLite Micro | ONNX Runtime / TFLite |
| 模型规模  | ~500 字节 ~ 100KB      | ~1MB ~ 100MB          |
| 典型场景  | 活动识别, 计步, 抬手亮屏       | 语音识别, 活动模式分析, 长期趋势预测  |
| 实时性要求 | <10ms                | <500ms                |
| 数据通信  | BLE GATT 特征值         | BLE GATT              |
| 推理频率  | 50Hz (每帧)            | 按需 / 定时同步             |

### 4.3 推荐路线

```
Phase 1-3 (已完成)               Phase 4 (当前)                Phase 5 (计划)
┌──────────────────────┐       ┌──────────────────────┐    ┌──────────────────────┐
│ 基础平台 + 通信 + 续航  │   →   │ 手机 App 开发          │ →  │ AI 增强 + 语音助手     │
│ 工具功能 + 音频 + AI  │       │ BLE 数据同步 + 通知管理  │    │ ESP-SR 唤醒           │
│ (RF 活动识别已部署)    │       │ 分布式 AI 推理         │    │ 开源 LLM 集成         │
└──────────────────────┘       └──────────────────────┘    └──────────────────────┘
```

### 4.4 适用 AI 场景

| AI 功能   | 方案                    | 部署位置 | 模型大小   | 优先级 |
| ------- | --------------------- | ---- | ------ | --- |
| 计步/活动识别 | RF 10 棵树 (已完成)        | 手表端  | ~500B  | P0  |
| 抬手亮屏    | IMU + 简单阈值 (已完成)      | 手表端  | -      | P0  |
| 活动模式分析  | ONNX 时序模型             | 手机端  | ~2MB   | P1  |
| 离线语音唤醒  | ESP-SR                | 手表端  | ~200KB | P1  |
| 语音命令识别  | TFLite Micro + 音频 CNN | 手表端  | ~100KB | P2  |
| 异常跌倒检测  | TFLite Micro + 1D CNN | 手表端  | ~30KB  | P2  |
| AI 语音助手 | ESP-SR 唤醒 + 开源 LLM    | 手机端  | -      | P2  |

---

## 5. 手表功能规划

### 5.1 功能矩阵

| 功能               | 类别  | 优先级 | 状态  | 说明                                              |
| ---------------- | --- | --- | --- | ----------------------------------------------- |
| 时间显示             | 核心  | P0  | ✅   | LVGL 数字表盘 + 模拟表盘 + RTC 校时                       |
| 触摸交互             | 核心  | P0  | ✅   | LVGL 滑动/点击/手势翻页 (8 页)                           |
| 背光控制             | 核心  | P0  | ✅   | 自动息屏 10s + 触摸/抬腕唤醒                              |
| 低功耗休眠            | 核心  | P0  | ✅   | Deep sleep 30s + 触摸/RTC 60s 唤醒                  |
| 计步器              | 健康  | P0  | ✅   | IMU 低通滤波 + 自适应基线峰值检测                            |
| 通知同步             | 通信  | P0  | ✅   | BLE GATT + UTF-8 中文双向                           |
| WiFi 校时          | 通信  | P0  | ✅   | WiFi 自动连接 + NTP → RTC                           |
| 抬手亮屏             | 交互  | P1  | ✅   | IMU 重力角度 vs 休止基线                                |
| 模拟表盘             | UI  | P1  | ✅   | LVGL line 时针/分针/秒针 + 12 刻度                      |
| 电池电量             | 系统  | P0  | ✅   | 原始 ADC 读取 (绕过 isBatteryConnect)                 |
| USB 充电保活         | 系统  | P1  | ✅   | 插 USB 跳过深睡，保持串口                                 |
| 天气显示             | 工具  | P1  | ✅   | Open-Meteo API, 温度/湿度/条件, 10 分钟刷新               |
| 秒表/倒计时           | 工具  | P1  | ✅   | 双模式 Start/Stop/Reset/Mode/+10s                  |
| TF 卡存储           | 系统  | P1  | ✅   | SDMMC 1-bit, FAT32, 文件枚举                        |
| 音频播放             | 媒体  | P1  | ✅   | ES8311 I2S 编解码器, WAV 播放, 音量控制                   |
| 音频播放器 UI         | 工具  | P1  | ✅   | Play/Stop/Next 按钮, TF 卡 .wav 列表                 |
| 端侧活动识别 AI        | AI  | P1  | ✅   | 随机森林 10 棵树, 50 帧窗口, 行走/跑步/静止分类                  |
| 模型训练管线           | AI  | P1  | ✅   | collect_data.py → train.py / train_rf.py → C 数组 |
| BLE 音乐控制         | 通信  | P1  | ❌   | HID 协议                                          |
| OTA 升级           | 系统  | P1  | ❌   | HTTP/BLE                                        |
| 跌倒检测             | 安全  | P2  | ❌   | AI 模型                                           |
| 语音助手             | AI  | P2  | ❌   | ESP-SR 离线唤醒 + 开源 LLM (端侧+云端混合)                  |
| 手机 App (数据同步)    | 通信  | P1  | ❌   | BLE 读取步数/电池/活动数据, 通知管理                          |
| 手机 App (AI 协同推理) | AI  | P1  | ❌   | 手机端跑大规模模型 + 手表端轻量模型, BLE 交换推理结果                 |
| 手机 App (健康看板)    | 健康  | P2  | ❌   | 日/周/月步数趋势, 活动分布图表, 历史记录                         |
| 手机 App (OTA 管理)  | 系统  | P2  | ❌   | 通过手机 App 推送固件更新, 版本管理                           |
| 自定义表盘            | UI  | P2  | ❌   | LVGL 动态加载                                       |

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
┌──────────────────────────────────────────────────────────────────┐
│                         手机 App (Flutter/Kotlin)                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │ BLE 通信  │ │ 数据看板  │ │ AI 推理   │ │ OTA 管理          │   │
│  │ (GATT)   │ │ 步数/活动 │ │ 大模型    │ │ 固件下载/推送       │   │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────────┬─────────┘   │
│       │            │            │                │              │
│  ┌────┴────────────┴────────────┴────────────────┴───────────┐ │
│  │                      数据处理层                                │ │
│  │  模型推理 (ONNX/TFLite) | 数据持久化 (SQLite) | 图表绘制        │ │
│  └─────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────┬───────────────────────────┘
                                       │ BLE GATT
┌──────────────────────────────────────┴───────────────────────────┐
│                         手表固件 (ESP32-S3)                        │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │                     应用层                                     │ │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     │ │
│  │  │ 表盘  │ │ 通知  │ │ 运动  │ │ 天气  │ │ 秒表  │ │ 播放器 │   │ │
│  │  └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──────┘   │ │
│  ├─────┼────────┼────────┼────────┼────────┼───────────────────┤ │
│  │  ┌──┴────────┴────────┴────────┴────────┴────────────────┐ │ │
│  │  │                    LVGL UI 层                           │ │ │
│  │  │           (8 页, 手势翻页, 动画)                        │ │ │
│  │  └──────────────────────────┬─────────────────────────────┘ │ │
│  │  ┌──────────────────────────┴─────────────────────────────┐ │ │
│  │  │                  服务层                                  │ │ │
│  │  │  BLE | WiFi | IMU | PMU | RTC | AI 推理 | TF卡 | Audio│ │ │
│  │  └──────────────────────────┬─────────────────────────────┘ │ │
│  │  ┌──────────────────────────┴─────────────────────────────┐ │ │
│  │  │                  硬件抽象层 (HAL)                        │ │ │
│  │  │  ST7789 | CST816S | QMI8658 | AXP2101 | BLE | I2S     │ │ │
│  │  └─────────────────────────────────────────────────────────┘ │ │
│  └──────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
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
│   ├── main.cpp              # 入口 + 主循环 (UI 更新/触摸/传感器/深睡)
│   ├── lv_port_disp.cpp/h    # LVGL 显示端口
│   ├── lv_port_indev.cpp/h   # LVGL 触控端口
│   ├── pin_config.h          # 引脚映射 (显示/触控/TF/音频)
│   ├── activity.cpp/h        # 活动识别 (RF 推理 + 50 帧滑动窗口)
│   ├── activity_model.h      # 随机森林模型数据 (10 棵决策树, C 数组)
│   ├── weather.cpp/h         # 天气页面 (Open-Meteo API)
│   ├── stopwatch.cpp/h       # 秒表/倒计时页面
│   ├── player.cpp/h          # 音频播放器页面 (TF .wav)
│   ├── service/              # 后台服务
│   │   ├── ble_srv.cpp/h     # BLE GATT 通知服务 (自定义 Profile)
│   │   ├── wifi_ntp.cpp/h    # WiFi 自动连接 + NTP 校时
│   │   ├── audio.cpp/h       # ES8311 I2S 音频驱动 + PCA9557
│   │   └── tf_card.cpp/h     # SDMMC 1-bit TF 卡驱动
├── include/
│   ├── pin_config.h
│   └── lv_conf.h
├── lib/                      # 本地库
│   ├── GFX_Library_for_Arduino/
│   ├── SensorLib-Waveshare/  # PCF85063 RTC + QMI8658 IMU
│   └── XPowersLib/           # AXP2101 PMU 驱动
├── boards/                   # 自定义板级定义
│   └── ESP32-S3-R8-OPI.json
├── training/                 # 模型训练管线
│   ├── collect_data.py       # 串口 IMU 数据采集 (标定行走/跑步/静止)
│   ├── model.py              # TinyHAR / TinyTCN 网络定义
│   ├── train.py              # PyTorch 训练 → ONNX 导出
│   ├── train_rf.py           # 随机森林训练 → C 数组导出
│   └── requirements.txt
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

### 已完成：增强功能（Phase 3 — 大部分完成）

**已完成**：

| 任务              | 状态  | 说明                                                    |
| --------------- | --- | ----------------------------------------------------- |
| 天气显示            | ✅   | Open-Meteo API, HTTP GET, 温度/湿度/条件, 每 10 分钟刷新         |
| 秒表/倒计时          | ✅   | 双模式, Start/Stop/Reset/Mode/+10s, `millis()` 计时        |
| TF 卡驱动          | ✅   | SDMMC 1-bit, FAT32, 文件列举, 容量查询                        |
| ES8311 音频编解码器驱动 | ✅   | I2S 主控 TX, PCA9557 PA_EN, 正弦波 + WAV 播放, 音量控制          |
| 音频播放器 UI        | ✅   | Play/Stop/Next 按钮, TF 卡 .wav 文件列表, 状态显示               |
| 端侧活动识别 AI       | ✅   | 50 帧滑动窗口 → 12 特征 (mean+std) → 随机森林 10 棵树 → 行走/跑步/静止分类 |
| 模型训练管线          | ✅   | collect_data.py → train.py / train_rf.py → C 代码导出     |

**剩余**：

| 周次  | 任务                             | 交付物    |
| --- | ------------------------------ | ------ |
| 1   | **OTA 升级**：HTTP 固件下载 + 差分更新    | 无线固件更新 |
| 2   | **BLE 音乐控制**                   | HID 遥控 |
| 3   | **综合调试 + 续航优化**：各模式电流测量，代码尺寸优化 | 稳定版发布  |

---

### 第三阶段：边缘 AI（Phase 4 — 4 周）

**目标**：端侧 AI 能力落地。

| 周次  | 任务                              | 交付物           |
| --- | ------------------------------- | ------------- |
| 1   | ~~TFLite Micro 集成 + ESP-NN 加速~~ | 已用 RF 方案替代    |
| 2   | ~~活动识别模型（行走/跑步/静止）~~            | ✅ 已完成 (RF 部署) |
| 3   | **ESP-SR 离线语音唤醒**               | "嘿 手表" 唤醒     |
| 4   | **异常检测（跌倒检测）**                  | 安全告警          |

**技术选型说明**：活动识别未使用 TFLite Micro，而是改用 **Scikit-learn RandomForest → C 数组** 的轻量方案。10 棵深度 ≤4 的决策树 + 12 个统计特征 (mean/std)，代码体积约 500 字节，推理时间 <1ms。如需更复杂的模型（CNN/时序），后续可引入 TFLite Micro。

**Python 训练前置任务（已完成）：**

- ✅ 数据采集脚本 `collect_data.py`（串口 IMU → CSV，实时标定行走/跑步/静止）
- ✅ PyTorch 模型训练 `train.py` → ONNX 导出
- ✅ 随机森林训练 `train_rf.py` → C 代码导出（`activity_model.h`）

---

### 第四阶段：手机 App + 分布式 AI（Phase 5 — 6 周）

**目标**：开发手机 App，实现手表数据同步 + 手机端 AI 推理 + OTA 管理。

**技术选型**：

- **Flutter 3.x**（跨平台，单代码库覆盖 Android/iOS）
- **flutter_blue_plus**（BLE GATT 通信）
- **TFLite / ONNX Runtime Mobile**（手机端 AI 推理）
- **SQLite / Isar**（本地数据持久化）
- **fl_chart**（健康数据可视化）

| 周次  | 任务                            | 交付物                  |
| --- | ----------------------------- | -------------------- |
| 1   | **BLE 通信层**：扫描/连接/读写 GATT 特征值 | flutter_blue_plus 集成 |
| 2   | **数据同步**：步数/电池/活动数据读取 + 本地缓存  | 手表→手机数据流             |
| 3   | **通知管理**：自定义推送, 消息过滤, 勿扰模式    | 通知配置页                |
| 4   | **AI 协同推理**：手机端 ONNX 活动识别模型   | 手机+手表双端推理            |
| 5   | **健康看板**：日/周/月趋势图, 活动分布       | 图表页                  |
| 6   | **OTA 管理**：固件下载 → BLE 分片推送    | 手机端固件更新              |

**分布式 AI 架构**：

```
┌─────────────────────┐     BLE GATT      ┌──────────────────────┐
│     手机 App         │ ◄────────────── ► │   手表 ESP32-S3      │
│                     │                    │                      │
│  大模型推理 (ONNX)    │                    │  轻量 RF/TFLite 推理    │
│  - 语音识别 (SenseVoice) │                    │  - 活动识别 (行走/跑步) │
│  - 活动模式分析 (更复杂) │                    │  - 计步 + 抬手亮屏     │
│  - 长期数据趋势       │                    │  - 实时传感器处理      │
└─────────────────────┘                    └──────────────────────┘
```

**开源 BLE App 参考项目**：

| 项目                         | 技术栈     | 参考价值             |
| -------------------------- | ------- | ---------------- |
| **wristband-app**          | Flutter | BLE 通信, 健康看板     |
| **BlueBreeze**             | Flutter | BLE 设备管理, 数据同步   |
| **Espressif BLE App**      | Kotlin  | ESP32 BLE 官方参考   |
| **nRF Connect for Mobile** | Kotlin  | BLE 调试, GATT 浏览器 |

---

## 8. 工具链与环境

### 8.1 开发环境

| 工具                         | 用途           | 状态                        |
| -------------------------- | ------------ | ------------------------- |
| **PlatformIO**             | 嵌入式构建        | ✅ 已安装 (espressif32@6.9.0) |
| **Visual Studio Code**     | IDE          | ✅ 已安装                     |
| **Python 3.12**            | 模型训练 + 工具脚本  | ✅ 已安装                     |
| **Flutter 3.x**            | 手机 App 跨平台开发 | ❌ 需安装                     |
| **Android Studio / Xcode** | 手机 App 构建    | ❌ 需安装                     |

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

### 8.4 服务配置

| 服务        | 配置                                   |
| --------- | ------------------------------------ |
| WiFi SSID | 硬编码在 `service/wifi_ntp.h` (当前: iQOO) |
| NTP 服务器   | `pool.ntp.org`, TZ offset 28800      |
| 天气 API    | Open-Meteo (HTTP, 北京 39.9/116.4)     |
| BLE 名     | `SmartBracelet`                      |

### 8.5 烧录方式

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

| 风险                        | 影响  | 概率  | 缓解措施                                                         |
| ------------------------- | --- | --- | ------------------------------------------------------------ |
| PSRAM 初始化失败（boot loop）    | 高   | 低   | 已解决：用标准板型 `esp32-s3-devkitc-1`，禁用 `-DBOARD_HAS_PSRAM`        |
| IRAM 溢出（TG0WDT 复位）        | 高   | 低   | 已解决：精简 lib_deps（仅 CST816S + lvgl + BLE + WiFi + ArduinoJson） |
| BLE + WiFi + LVGL 共存内存不足  | 中   | 低   | 当前 RAM 148KB/320KB (45%)，余量 170KB+，Bluedroid 工作正常            |
| BLE + WiFi 共存射频干扰         | 中   | 低   | 分时复用，连接 WiFi 时 BLE 暂停广播                                      |
| Deep sleep 唤醒失败           | 高   | 低   | 已验证：触摸 INT 唤醒 + RTC 60s 定时唤醒均正常工作                            |
| 续航不足                      | 高   | 低   | Deep sleep 已实现，待测量各模式实际电流                                    |
| 电池保护板锁死（过放）               | 中   | 中   | 已知硬件问题：保护板触发后需断开重接或换电池，软件已做兼容（直接读 ADC）                       |
| USB 串口静默崩溃                | 高   | 低   | 已验证：全局 `new` 放入 `setup()`；USBSerial 加 3 秒超时；不自定义板 JSON       |
| RTS 复位不可靠                 | 中   | 高   | 上传后手动拔插 USB 冷启动                                              |
| ES8311 音频 I2S DMA 缓冲区溢出   | 中   | 低   | I2S DMA buf 8×256，WAV 播放使用独立 FreeRTOS 任务 (4096 栈)            |
| RF 活动识别模型泛化能力不足（仅单用户数据训练） | 中   | 中   | 当前模型用 10 棵树 + 3 折交叉验证训练，收集更多用户数据可提升泛化性                       |
| 手机 App + 手表 BLE 通信兼容性     | 中   | 中   | Flutter BLE 插件 (flutter_blue_plus) 适配 Android/iOS，需处理重连/断连状态 |
| 手机端 AI 模型大小 + 推理性能        | 中   | 低   | ONNX Runtime Mobile + INT8 量化，主流手机均可实时推理                     |
| 跨平台 App 开发工作量 (Flutter)   | 中   | 中   | BLE + 图表 + 本地数据库均 Flutter 生态成熟方案, 单代码库覆盖双平台                  |

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
| flutter_blue_plus          | https://github.com/cunarist/flutter_blue_plus             | Flutter BLE 通信库                  |
| nRF Connect for Mobile     | https://github.com/NordicSemiconductor/nRF-Connect        | BLE 调试 + GATT 参考实现               |
| Wearable-AI 参考架构           | https://github.com/microsoft/hummingbird                  | 端侧 AI → 手机侧 AI 协同模式参考            |

### B. 已调试经验

见 `DEBUG_REPORT.md` — 包含以下 10 次调试记录：

1. **白屏/启动循环** — USBSerial 重复定义、全局 new 崩溃
2. **USB 插拔后 flash 损坏** — 全片擦除 + esptool 手动三段式烧录
3. **LVGL 集成** — 显示缓冲区、颜色字节序 (SWAP=0)、配置路径
4. **LVGL 验证 + 触摸集成** — RST 复位、触摸引脚 19/20 → 15/14 迁移
5. **Boot Loop 修复** — Arduino_DriveBus → CST816S 回退、精简依赖
6. **传感器集成 + 表盘 UI** — RTC/IMU/PMU 全通、LVGL 多页面表盘
7. **BLE 双向通信 + 中文 UTF-8** — NimBLE → ESP32 BLE Arduino 迁移
8. **抬手亮屏 + 通知页 + Deep sleep** — IMU 重力角度抬腕检测、息屏/深睡状态机
9. **顶部花屏 + 电池诊断 + 充电保活** — ST7789 越界像素清除、ADC 原始读取、USB 跳过深睡
10. **ES8311 音频 + PCA9557 + I2S 调试** — 编解码器时钟配置、功放使能、DMA 缓冲

### C. 训练管线

见 `training/` 目录：

- `collect_data.py` — 通过串口采集 IMU 实时数据，按键标定活动类型 (1=行走, 2=跑步, 4=静止)
- `train.py` — PyTorch TinyHAR/TinyTCN 训练，输出 `.pt` 权重 + `.onnx` 模型
- `train_rf.py` — 随机森林训练，直接输出可嵌入固件的 C 数组代码
- `model.py` — TinyHAR (CNN+GRU) 和 TinyTCN 网络定义
