# SmartBracelet — 基于 ESP32-S3 的智能手表

> 一款面向嵌入式大赛 / 个人学习项目的开源智能手表：ESP32-S3 固件（Arduino + LVGL）
> 负责显示、传感与交互，PC 桌面端（PyQt6）负责遥测监控与语音助手桥接，
> 配套一套从数据采集到模型导出的端侧 AI 训练管线。

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)]()
[![Framework](https://img.shields.io/badge/framework-Arduino%20%2B%20PlatformIO-teal)]()
[![UI](https://img.shields.io/badge/UI-LVGL%208.4-green)]()
[![Desktop](https://img.shields.io/badge/desktop-PyQt6-orange)]()
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

---

## 目录

- [1. 项目简介](#1-项目简介)
- [2. 功能特性](#2-功能特性)
- [3. 系统架构](#3-系统架构)
- [4. 目录结构](#4-目录结构)
- [5. 环境依赖](#5-环境依赖)
- [6. 安装与运行](#6-安装与运行)
- [7. 使用示例](#7-使用示例)
- [8. 串口协议（API）说明](#8-串口协议api说明)
- [9. 固件模块接口](#9-固件模块接口)
- [10. 配置项说明](#10-配置项说明)
- [11. 已知问题与注意事项](#11-已知问题与注意事项)
- [12. 贡献指南](#12-贡献指南)
- [13. 许可证](#13-许可证)
- [14. 相关文档](#14-相关文档)

---

## 1. 项目简介

SmartBracelet 是一套完整的智能手表软硬件方案，围绕 **Waveshare ESP32-S3-Touch-LCD-1.83**
开发板构建。项目由三部分组成：

| 组成部分 | 技术栈 | 职责 |
| -------- | ------ | ---- |
| **手表固件** | C++ / Arduino / PlatformIO / LVGL 8.4 | 显示、触摸、传感器采集、电源管理、本地 UI、串口通信 |
| **桌面监控端** | Python / PyQt6 / pyqtgraph | USB 串口遥测可视化、通知下发、语音助手（ASR + LLM）桥接、OTA 触发 |
| **训练管线** | Python / PyTorch / scikit-learn | 采集 IMU 数据 → 训练模型 → 导出可嵌入固件的 C 数组 |

固件与桌面端通过 **USB CDC 串口（115200 bps）** 使用 **逐行 JSON** 协议双向通信：
手表上行遥测、事件与语音音频；桌面端下行通知、时间同步、定位、OTA 与语音识别结果。

设计目标：在有限的 RAM / Flash 资源下，用清晰的分层结构实现一块**可日常佩戴、可二次开发**的智能手表。

---

## 2. 功能特性

### 2.1 固件功能

| 模块 | 功能 | 状态 |
| ---- | ---- | ---- |
| 显示 | Arduino_GFX（ST7789）+ LVGL 8.4.0，240×284，偏移 (0, 20) | ✅ |
| 触摸 | CST816D（`fbiego/CST816S` 驱动，I2C 0x15）：左右滑翻页、上下滑、长按切表盘、点击 | ✅ |
| 表盘 | 数字表盘 / 模拟表盘（时分秒针 + 12 刻度）/ 运动表盘（步数环 + 卡路里） | ✅ |
| 页面导航 | 表盘、模拟、传感器、通知、秒表、天气、语音、设置，共 8 个逻辑页面 | ✅ |
| 传感器 | QMI8658 六轴 IMU，独立 FreeRTOS 任务在 Core 0 以 125 Hz 采集 | ✅ |
| 计步 | 低通滤波 + 自适应基线 + 峰值时间窗校验，NVS 持久化与跨天自动重置 | ✅ |
| 抬手亮屏 | 重力矢量低通滤波 + 休止基线夹角判定（>25°） | ✅ |
| 跌倒检测 | 状态机：自由落体 → 撞击 → 静止，三段判定后报警 | ✅ |
| 运动强度 | 加速度方差滑窗 → 0-100 强度、METs 与卡路里估算 | ✅ |
| 电池健康 | 充电周期统计与满充电压衰减估算健康度 | ✅ |
| RTC | PCF85063 时间读写 | ✅ |
| WiFi / NTP | STA 连接 + NTP 校时（`pool.ntp.org`），按需开关射频省电 | ✅ |
| 电量 / 充电 | AXP2101 直接读 ADC 寄存器获取电压与百分比，充电状态识别 | ✅ |
| 通知 | 环形缓冲通知历史 + 独立通知页，DND 免打扰开关 | ✅ |
| 秒表 / 倒计时 | 双模式、Start/Stop/Reset/Mode/+10s | ✅ |
| 天气 | Open-Meteo 免费 API，温度/湿度/天气状况，30 分钟缓存 | ✅ |
| 音乐提示音 | ES8311 编解码器（I2S TX）+ PCA9557 功放使能，正弦提示音 | ✅ |
| 语音助手（端侧采集） | INMP441 I2S 麦克风 → ADPCM 压缩 → Base64 → 串口上传；结果回显 | ✅ |
| 快捷面板 | 下拉面板：WiFi / DND / 亮度滑块 | ✅ |
| 设置页 | 步数目标、亮度、DND、表盘选择、电池健康、固件版本 | ✅ |
| 持久化 | NVS 保存步数、昨日步数、设置、表盘、崩前页面、电池周期 | ✅ |
| 崩溃恢复 | 依据 `esp_reset_reason()` 恢复崩溃前页面与步数 | ✅ |
| 按键 | BOOT（GPIO0，中断 + 长短按）与 AXP2101 PWR 键 | ✅ |
| OTA | WiFi HTTP 固件下载 + BLE 分片写入两套实现 | ✅（已实现，需配合分发端） |
| 屏幕休眠 | 60 s 无操作自动熄屏，触摸 / 抬腕 / 按键唤醒；息屏时降 CPU 频率与采样率 | ✅ |

> 说明：为控制镜像体积与 IRAM 占用，当前构建**未启用 BLE**，与 PC 的通信统一走 USB 串口。
> 早期的 BLE 通知 / DataService / HID 实现与端侧随机森林活动识别已从固件中裁撤，
> 相关训练代码仍保留在 `training/` 中。

### 2.2 桌面监控端功能

| 功能 | 说明 |
| ---- | ---- |
| 串口管理 | 自动枚举串口，2 s 轮询刷新，一键连接 / 断开 |
| 总览页 | 电量 / 电压 / 充电 / USB、步数 / WiFi、卡路里 / 强度 / METs 卡片 + 电池与步数曲线 |
| 传感器页 | IMU 实时读数 + 三轴加速度曲线 + 运动强度曲线 |
| 语音 AI 页 | MiMo / OpenAI 兼容 SSE 流式对话；手表麦克风音频经 ASR 转写后送入 LLM |
| 控制台页 | 发送通知、发送原始 JSON 命令、快捷 DND / 时间同步、日志视图 |
| 跌倒告警 | 收到 `FALL` 事件时闪烁红色横幅，需手动 ACK |
| 音频回放链路 | 接收手表 ADPCM 分片 → IMA ADPCM 解码 → 生成 WAV → 调用 Whisper 兼容 ASR |

### 2.3 训练管线功能

| 脚本 | 功能 |
| ---- | ---- |
| `training/collect_data.py` | 经串口实时采集 IMU 六轴数据，按键标注活动类型 → CSV |
| `training/train_rf.py` | 随机森林（10 棵树 / 深度 ≤4）+ 12 维统计特征 → 导出可直接编译的 C 数组 |
| `training/train.py` | PyTorch 训练 TinyHAR / TinyTCN，输出 `.pt` 检查点与 `.onnx` |
| `training/model.py` | TinyHAR（1D-CNN）与 TinyTCN（膨胀卷积）网络定义 |
| `training/archive/dataset.py` | 滑动窗口切分、数据增强、train/val/test 划分 |
| `training/archive/convert.py` | PyTorch → Keras 权重复制 → TFLite（FP32 / INT8）→ C 头文件 |

---

## 3. 系统架构

```
┌────────────────────────────────────────────────────────────┐
│                    PC 桌面端 (PyQt6)                        │
│  串口遥测 · 曲线绘制 · 通知下发 · ASR/LLM 语音助手 · OTA     │
└───────────────────────────┬────────────────────────────────┘
                            │ USB CDC 串口，逐行 JSON @115200
┌───────────────────────────┴────────────────────────────────┐
│                  手表固件 (ESP32-S3, Arduino)               │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ 应用层  表盘 / 传感器 / 通知 / 秒表 / 天气 / 语音 / 设置 │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ UI 层   LVGL 8.4（8 页面 · 状态栏 · 快捷面板 · 样式）    │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ 服务层  串口协议 · WiFi/NTP · OTA · 音频 · 语音助手      │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ 算法层  计步 · 抬手 · 跌倒 · 运动强度 · 电池健康         │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ 任务层  sensor_task(125Hz, Core0) + 主循环(Core1)      │  │
│  ├──────────────────────────────────────────────────────┤  │
│  │ HAL     ST7789 · CST816D · QMI8658 · AXP2101 · PCF85063│  │
│  │         ES8311 · PCA9557 · INMP441 · NVS               │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
```

**并发模型**：传感器采集运行在 Core 0 的独立任务（优先级 5，低于协议栈，避免饿死网络/蓝牙栈），
主循环运行在 Core 1。IMU 共享数据通过互斥量（`sensor_data_lock / unlock`）保护。

---

## 4. 目录结构

```
SmartBracelet/
├── src/                          # 固件源码
│   ├── main.cpp                  # 入口：setup/loop、页面管理、遥测、电源与按键
│   ├── ui_pages.cpp / .h         # LVGL 页面创建与更新（表盘/传感器/通知/模拟）
│   ├── ui_styles.cpp / .h        # 全局配色与共享 LVGL 样式
│   ├── watch_faces.cpp / .h      # 表盘管理（数字/模拟/运动）与切换逻辑
│   ├── settings_page.cpp / .h    # 设置页（步数目标/亮度/DND/表盘/电池健康）
│   ├── quick_panel.cpp / .h      # 下拉快捷面板（WiFi/DND/亮度）
│   ├── sensor_task.cpp / .h      # IMU FreeRTOS 采集任务与数据锁
│   ├── step_counter.cpp / .h     # 计步算法
│   ├── wrist_detect.cpp / .h     # 抬手亮屏检测
│   ├── fall_detect.cpp / .h      # 跌倒检测状态机
│   ├── motion_intensity.cpp / .h # 运动强度 / METs / 卡路里
│   ├── batt_health.cpp / .h      # 电池健康（周期 / 满充电压）
│   ├── backlight.cpp / .h        # 背光 PWM 与屏幕开关状态
│   ├── nvs_store.cpp / .h        # NVS 持久化封装
│   ├── notif_history.cpp / .h    # 通知历史环形缓冲
│   ├── stopwatch.cpp / .h        # 秒表 / 倒计时页面
│   ├── weather.cpp / .h          # 天气页面（Open-Meteo）
│   ├── serial_protocol.cpp / .h  # 串口 JSON 协议（上行遥测 / 下行命令）
│   ├── voice_chat_ui.cpp / .h    # 语音助手 LVGL 页面
│   ├── lv_port_disp.cpp / .h     # LVGL 显示移植（flush 回调）
│   ├── lv_port_indev.cpp / .h    # LVGL 触摸输入移植
│   ├── debug_log.h               # 分级日志宏（ERR/WARN/INFO/DEBUG/VERBOSE）
│   └── service/                  # 后台服务
│       ├── wifi_ntp.cpp / .h         # WiFi 连接、NTP 校时、射频电源管理
│       ├── audio.cpp / .h            # ES8311 + PCA9557 + I2S 收发
│       ├── adpcm.c / .h              # IMA ADPCM 编解码
│       ├── voice_assistant.cpp / .h  # 端侧语音录制/压缩/上传状态机
│       ├── voice_chat.cpp / .h       # 手机/PC 侧语音命令兼容层
│       └── ota_update.cpp / .h       # OTA（HTTP 下载 + BLE 分片）
├── include/
│   ├── pin_config.h              # 引脚映射与 I2C 从机地址（唯一版本）
│   └── lv_conf.h                 # LVGL 8.4 配置
├── lib/                          # 本地第三方库
│   ├── GFX_Library_for_Arduino/  # 显示驱动（ST7789 等）
│   ├── SensorLib-Waveshare/      # PCF85063 RTC + QMI8658 IMU 驱动
│   └── XPowersLib/               # AXP2101 PMU 驱动
├── boards/
│   └── ESP32-S3-R8-OPI.json      # 自定义板级定义（16MB Flash / 8MB OPI PSRAM）
├── desktop_app/                  # PC 桌面监控端（PyQt6）
│   ├── main.py                   # 程序入口与全局 QSS 主题
│   ├── telemetry_view.py         # 主窗口：总览/传感器/语音/控制台四个页签
│   ├── serial_io.py              # 串口线程、JSON 解析、TelemetryData
│   ├── audio_processor.py        # IMA ADPCM 解码、WAV 生成、Whisper ASR
│   ├── mimo_client.py            # OpenAI 兼容 SSE 流式对话客户端
│   ├── xiaozhi_client.py         # 小智 WebSocket 协议客户端（预留，暂未接入 UI）
│   ├── requirements.txt
│   └── run.bat                   # Windows 一键创建 venv 并启动
├── training/                     # 端侧 AI 训练管线
│   ├── collect_data.py           # IMU 数据采集
│   ├── train_rf.py               # 随机森林 → C 数组
│   ├── train.py                  # PyTorch 训练（TinyHAR / TinyTCN）
│   ├── model.py                  # 网络定义
│   ├── requirements.txt
│   ├── sample_data/              # 示例数据集（无需硬件即可跑通训练）
│   │   └── imu_walk_run_idle.csv
│   └── archive/                  # 归档：TFLite 转换与数据集切分
│       ├── dataset.py
│       └── convert.py
├── .vscode/
│   └── extensions.json           # 推荐插件（其余 .vscode 配置由 PlatformIO 生成，不入库）
├── platformio.ini                # PlatformIO 构建配置
├── LICENSE                       # MIT 开源许可证
├── CLAUDE.md                     # 硬件速查与开发交接说明
├── DEVELOPMENT_PLAN.md           # 开发计划与里程碑
├── EDGE_AI_TRAINING_PLAN.md      # 边缘 AI 方案
├── DEBUG_REPORT.md               # 调试记录
└── WORK_REPORT.md                # 开发工作报告
```

### 4.1 版本控制约定

本仓库只提交**源代码与文档**，以下内容通过 `.gitignore` 排除：

| 类别 | 示例 |
| ---- | ---- |
| 构建产物与依赖缓存 | `.pio/`（PlatformIO 构建输出、`libdeps`） |
| 编辑器 / IDE 本机配置 | `.idea/`、`*.iml`、`.vscode/c_cpp_properties.json`、`.vscode/launch.json`、`.vscode/settings.json` |
| 代码索引 / AI 工具缓存 | `.codeatlas/`、`.codegraph/`、`.qoder/`、`traces/`、`.claude/settings.local.json` |
| Python 环境与缓存 | `.venv/`、`venv/`、`__pycache__/`、`*.pyc` |
| 训练与调试产物 | `*.pt`、`*.onnx`、`*.tflite`、`test_audio.wav` |
| 系统临时文件 | `nul`、`Thumbs.db`、`Desktop.ini`、`.DS_Store` |

`.vscode/c_cpp_properties.json`、`launch.json`、`settings.json` 由 PlatformIO 按本机路径**自动重新生成**，
因此不入库；`.vscode/extensions.json`（推荐插件）为跨环境通用配置，保留在版本控制中。

---

## 5. 环境依赖

### 5.1 固件开发

| 依赖 | 版本 / 说明 |
| ---- | ----------- |
| [PlatformIO Core](https://platformio.org/) | 开发时使用 espressif32@6.9.0 |
| Python | 3.8+（供 PlatformIO / esptool 使用） |
| 开发板 | Waveshare ESP32-S3-Touch-LCD-1.83（ESP32-S3R8，16 MB Flash） |

`platformio.ini` 声明的库依赖：

- `fbiego/CST816S` — 触摸驱动（兼容 CST816D）
- `lvgl/lvgl@^8.4.0` — 图形库
- `bblanchon/ArduinoJson` — JSON 序列化

`lib/` 下的本地库：`GFX_Library_for_Arduino`、`SensorLib-Waveshare`、`XPowersLib`。

### 5.2 桌面监控端

| 依赖 | 版本 |
| ---- | ---- |
| Python | 3.8+（开发环境 3.11） |
| PyQt6 | ≥ 6.5 |
| pyqtgraph | ≥ 0.13 |
| pyserial | ≥ 3.5 |
| websocket-client | ≥ 1.7 |

### 5.3 训练管线

| 依赖 | 版本 |
| ---- | ---- |
| Python | 3.9+（开发环境 3.12） |
| torch | ≥ 2.0 |
| scikit-learn | ≥ 1.3 |
| numpy | ≥ 1.24 |
| pyserial | ≥ 3.5 |

---

## 6. 安装与运行

### 6.1 固件：编译与烧录

```bash
# 安装 PlatformIO Core（如尚未安装）
pip install platformio

# 编译
pio run

# 烧录（默认 upload_port = COM9，按实际串口修改 platformio.ini）
pio run -t upload

# 查看串口日志
pio device monitor
```

若 `pio run -t upload` 在本板卡住，可改用 esptool 手动三段式烧录
（该板 eFuse 锁定 QIO，必须使用 `--flash_mode qio`；115200 波特率最稳定）：

```bash
python "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" \
    --chip esp32s3 --port COM9 --baud 115200 \
    --before default_reset --after hard_reset write_flash -z \
    --flash_mode qio --flash_freq 80m --flash_size 16MB \
    0x0 .pio/build/esp32s3/bootloader.bin \
    0x8000 .pio/build/esp32s3/partitions.bin \
    0x10000 .pio/build/esp32s3/firmware.bin
```

> 若 flash 损坏导致板子无法启动：进入下载模式（按住 BOOT 再插 USB，随后松开 BOOT），
> 先 `esptool.py erase_flash`，再重新烧录。

### 6.2 桌面监控端

**Windows 一键启动：**

```bat
cd desktop_app
run.bat
```

`run.bat` 会自动创建 `.venv`、安装 `requirements.txt` 并启动程序。

**手动启动：**

```bash
cd desktop_app
python -m venv .venv
.venv\Scripts\activate          # Linux/macOS: source .venv/bin/activate
pip install -r requirements.txt
python main.py
```

### 6.3 训练管线

```bash
cd training
pip install -r requirements.txt

# 1) 采集数据（手表经 USB 连接，按提示用数字键标注活动类型）
python collect_data.py COM9 --baud 115200

# 2) 训练随机森林并打印可嵌入固件的 C 代码
python train_rf.py imu_data_20260521_120000.csv
```

---

## 7. 使用示例

### 7.1 手表操作

| 操作 | 效果 |
| ---- | ---- |
| 左右滑动 | 切换页面 |
| 上滑 | 点亮屏幕并重置息屏计时 |
| 下滑 | 打开 / 关闭下拉快捷面板 |
| 长按屏幕 | 切换表盘（数字 → 模拟 → 运动） |
| 短按 BOOT / PWR | 开 / 关屏幕 |
| 长按 BOOT | 切换表盘；若正在录音则取消录音 |
| 短按 PWR | 打开 / 关闭快捷面板；若语音助手激活则取消 |

### 7.2 桌面端下发通知

在 **Console** 页签填写 app / title / body，点击 **Send Notify**，
手表通知页会收到并在通知历史中显示：

```json
{"c":"notify","app":"WeChat","title":"新消息","body":"今晚一起吃饭吗？"}
```

### 7.3 通过控制台发送原始命令

在 **Console** 页签的输入框直接输入 JSON 命令（详见[第 8 节](#8-串口协议api说明)），例如时间同步：

```json
{"c":"time","epoch":1748000000}
```

### 7.4 语音助手链路

```
手表录音 → ADPCM 压缩 → 串口分片(Base64) → 桌面端重组/解码 → WAV
      → Whisper 兼容 ASR → LLM 生成回复 → 串口回传 → 手表显示
```

1. 在手表「语音」页面点击麦克风按钮开始录音（或长按相关按键），再次点击结束；
2. 桌面端 **Voice AI** 页签会显示“Receiving audio… → Sending to ASR…”，并填入 API Key / Base URL；
3. ASR 转写结果自动送入 LLM，回复经 `{"c":"voice","vc":"va_result",...}` 回传到手表显示。

### 7.5 嵌入训练好的随机森林模型

`train_rf.py` 会输出 `RFNode` 结构体、`tree_N[]` 数组与 `rf_predict()` 函数，
将其复制到固件（例如新建头文件），在 IMU 数据准备就绪后按 50 帧窗口计算
「6 轴均值 + 6 轴标准差」共 12 维特征，调用 `rf_predict()` 即可得到活动类别索引。

---

## 8. 串口协议（API）说明

固件与桌面端通过 USB CDC 串口以 **逐行 JSON** 通信，速率 115200。
每个报文占一行，以 `\n` 结束。

### 8.1 上行（手表 → PC）

| 类型 | `e` 字段 | 说明 | 示例 |
| ---- | -------- | ---- | ---- |
| 遥测 | `t` | 周期性遥测 | `{"e":"t","b":85,"mv":4021,"chg":1,"usb":1,"st":1234,"wifi":1,"acc":[0.01,-0.02,0.99],"gyr":[0.1,0.2,0.3],"int":12,"met":1.8,"cal":35.2}` |
| 事件 | 自定义 | 事件告警，如跌倒 | `{"e":"a","msg":"FALL_DETECTED"}` |
| 音频开始 | `va` / `s:"start"` | 语音录音开始上行 | `{"e":"va","s":"start","len":4096}` |
| 音频分片 | `va` / `s:"data"` | Base64 编码的 ADPCM 数据 | `{"e":"va","s":"data","seq":3,"d":"..."}` |
| 音频结束 | `va` / `s:"end"` | 末分片序号 | `{"e":"va","s":"end","seq":7}` |

**遥测字段说明**

| 字段 | 含义 | 单位 |
| ---- | ---- | ---- |
| `b` | 电池百分比 | % |
| `mv` | 电池电压 | mV |
| `chg` | 是否充电 | 0/1 |
| `usb` | 是否接入 USB | 0/1 |
| `st` | 当日步数 | 步 |
| `wifi` | WiFi 是否连接 | 0/1 |
| `acc` | 加速度 X/Y/Z | g |
| `gyr` | 角速度 X/Y/Z | °/s |
| `int` | 运动强度 | 0-100 |
| `met` | 代谢当量 | METs |
| `cal` | 累计卡路里 | kcal |

### 8.2 下行（PC → 手表）

| `c` 字段 | 附加字段 | 说明 |
| -------- | -------- | ---- |
| `notify` | `app`、`title`、`body` | 推送一条通知，写入通知历史 |
| `time` | `epoch`（Unix 秒） | 同步系统时间 |
| `ota` | `url` | 启动 HTTP OTA 固件升级 |
| `dnd` | `on`（0/1） | 设置免打扰开关 |
| `loc` | `lat`、`lon` | 设置天气查询经纬度（持久化到 NVS） |
| `voice` | `vc`、`arg` 或 `msg`/`trans`/`resp` | 语音助手命令通道 |

**`voice` 子命令**

| `vc` 值 | 附加字段 | 说明 |
| ------- | -------- | ---- |
| `va_result` | `trans`（转写）、`resp`（回复） | 语音识别与 AI 回复结果 |
| `va_error` | `msg` | 语音链路错误 |
| 其它（`start` / `stop` / `result` / `error`） | `arg` | 兼容旧的手机侧语音命令 |

**下行示例**

```json
{"c":"notify","app":"SMS","title":"验证码","body":"123456"}
{"c":"time","epoch":1748000000}
{"c":"dnd","on":1}
{"c":"loc","lat":"31.2304","lon":"121.4737"}
{"c":"ota","url":"http://example.com/firmware.bin"}
{"c":"voice","vc":"va_result","trans":"今天天气如何","resp":"晴，18℃"}
```

---

## 9. 固件模块接口

各模块均以 C 风格函数或 C++ 类暴露接口，头文件位于 `src/` 与 `src/service/`。

### 9.1 传感器任务（`sensor_task.h`）

```c
void sensor_task_start(SensorQMI8658 *imu_ptr);  // 启动 Core 0 采集任务
void sensor_task_set_rate(int hz);               // 125=活跃, 25=息屏, 0=暂停
void sensor_data_lock(void);                     // 读取共享 IMU 数据前加锁
void sensor_data_unlock(void);                   // 读取完成后解锁
extern imu_data_t acc, gyr;                       // 共享加速度/角速度
```

### 9.2 串口协议（`serial_protocol.h`）

```c
void serial_push_telemetry(const ui_telemetry_t *t);  // 上行遥测
void serial_push_event(const char *type, const char *msg);
void serial_push_audio_start(int total_adpcm_bytes);
void serial_push_audio_chunk(int seq, const char *base64_data);
void serial_push_audio_end(int last_seq);
void serial_protocol_process(void);                   // 主循环中调用，解析下行命令
bool serial_notification_has_new(void);               // 是否收到新通知
const char* serial_notification_app(void);
const char* serial_notification_title(void);
const char* serial_notification_body(void);
void serial_notification_consume(void);
```

### 9.3 语音助手（`service/voice_assistant.h`）

```c
void        va_init(void);
bool        va_start_recording(void);
bool        va_stop_recording(void);
void        va_process(void);       // 主循环中调用
void        va_on_result(const char *transcription, const char *response);
void        va_on_error(const char *msg);
va_state_t  va_get_state(void);     // VA_IDLE/RECORDING/SENDING/WAITING/RESPONSE/ERROR
float       va_get_progress(void);  // 0.0 ~ 1.0
void        va_dismiss(void);
```

### 9.4 NVS 持久化（`nvs_store.h`）

```c
void nvs_store_init(void);
int  nvs_get_steps_today(void);      void nvs_set_steps_today(int steps);
int  nvs_get_step_goal(void);        void nvs_set_step_goal(int goal);
int  nvs_get_brightness(void);       void nvs_set_brightness(int level);
bool nvs_get_dnd(void);              void nvs_set_dnd(bool enable);
void nvs_set_watch_face(int face);   int  nvs_get_watch_face(void);
bool nvs_check_daily_reset(int current_day);   // 跨天时返回 true 并轮转步数
void nvs_set_crash_page(int page);   int  nvs_get_crash_page(void);
void nvs_set_weather_lat(const char *lat);  void nvs_get_weather_lat(char *buf, int maxlen);
void nvs_set_weather_lon(const char *lon);  void nvs_get_weather_lon(char *buf, int maxlen);
```

### 9.5 训练脚本导出的 C 推理接口（`train_rf.py` 输出）

```c
typedef struct { int f; float t; int l; int r; int cls; } RFNode;
static const RFNode tree_N[...];          // 每棵决策树的节点数组
static int rf_predict(const float features[12]);  // 返回类别索引
```

---

## 10. 配置项说明

### 10.1 `platformio.ini`

| 配置 | 当前值 | 说明 |
| ---- | ------ | ---- |
| `platform` | `espressif32@6.9.0` | 平台版本 |
| `board` | `esp32-s3-devkitc-1` | 使用标准板型（自定义板型会引发 `USBSerial` 未定义） |
| `board_build.flash_mode` | `qio` | 该板 eFuse 锁定 QIO，**不可改为 dio** |
| `upload_speed` | `115200` | 更高波特率容易 `Packet content transfer stopped` |
| `upload_port` | `COM9` | 按实际串口修改 |
| `build_flags` | `-Os`、`-DLV_CONF_INCLUDE_SIMPLE`、`-Iinclude`、WiFi IRAM 关闭 | 控制体积与 IRAM 占用 |

### 10.2 `include/pin_config.h`

引脚映射唯一来源（显示、触摸、I2C、TF 卡、I2S、麦克风）与 I2C 从机地址：

| 设备 | 地址 | 说明 |
| ---- | ---- | ---- |
| CST816D | 0x15 | 触摸 |
| PCF85063 | 0x51 | RTC |
| QMI8658 | 0x6A | IMU |
| AXP2101 | 0x34 | PMU |
| ES8311 | 0x18 | 音频编解码器 |
| PCA9557 | 0x19 | I/O 扩展（PA_EN） |

### 10.3 `include/lv_conf.h`

LVGL 8.4 配置。关键点：`LV_COLOR_16_SWAP` 必须为 **0**
（Arduino_GFX 的 `draw16bitRGBBitmap()` 已处理字节序）；`LV_SPRINTF_USE_FLOAT` 关闭，
因此浮点数需用整数格式化输出。

### 10.4 固件运行参数

| 位置 | 常量 | 说明 |
| ---- | ---- | ---- |
| `service/wifi_ntp.h` | `WIFI_SSID` / `WIFI_PASS` | WiFi 凭据（**演示值，部署前请修改**） |
| `service/wifi_ntp.h` | `NTP_SERVER` / `TZ_OFFSET` | NTP 服务器与时区偏移（秒） |
| `weather.cpp` | `WEATHER_REFRESH_MS` | 天气缓存刷新间隔（默认 30 分钟） |
| `weather.cpp` | `WEATHER_LAT` / `WEATHER_LON` | 默认经纬度；若收到 `loc` 命令则优先使用 NVS 中的值 |
| `debug_log.h` | `LOG_LEVEL` | 日志等级 0-5（默认 3 = INFO） |
| `main.cpp` | `DISPLAY_TIMEOUT_MS` | 熄屏超时（默认 60000 ms） |
| `main.cpp` | `NVS_SAVE_INTERVAL_MS` | NVS 保存间隔（默认 60000 ms） |
| `motion_intensity.cpp` | `DEFAULT_WEIGHT_KG` | 卡路里估算体重（默认 65 kg） |

### 10.5 桌面端

- **MiMo / LLM**：API Key、模型名（默认 `mimo-v2.5`）、Base URL 均可在「Voice AI」页签实时修改；
  接口需兼容 OpenAI `chat/completions` 的 SSE 流式返回。
- **ASR**：使用 OpenAI 兼容的 `audio/transcriptions` 接口（默认模型 `whisper-1`，语言 `zh`），见 `audio_processor.py`。

---

## 11. 已知问题与注意事项

1. **USB 带电插拔可能损坏 flash** — 触发保护板后需 `erase_flash` 后重新烧录，操作中勿拔 USB。
2. **RTS 复位不可靠** — 上传后如未自动启动，需手动拔插 USB 冷启动。
3. **PSRAM 当前未启用** — 模组具备 8 MB OPI PSRAM，但因初始化失败（`PSRAM ID read error`）
   与 IRAM 占用问题，构建中未定义 `-DBOARD_HAS_PSRAM`；语音缓冲区会自动回退到内部 SRAM。
4. **触摸唤醒约束** — 深睡唤醒依赖 CST816D 的 INT 引脚（GPIO13）保持有效。
5. **电池保护板锁死** — 过放触发保护后 AXP2101 无法充电，需物理断开电池排线重试或更换电池；
   固件已通过直接读取 ADC 规避 `isBatteryConnect()` 检测位。
6. **WiFi 凭据硬编码** — 见 `service/wifi_ntp.h`，请勿将真实凭据提交到公开仓库。
7. **`train.py` 依赖归档模块** — `dataset.py` 位于 `training/archive/`，脚本已做兼容导入；
   该 TFLite 路线当前为归档状态，主力方案是 `train_rf.py`。
8. **`desktop_app/xiaozhi_client.py`** 暂未被 UI 引用，为后续小智 WebSocket 语音接入预留。

---

## 12. 贡献指南

欢迎提交 Issue 与 Pull Request。为保证固件稳定性，请遵循以下约定：

### 12.1 提交流程

1. 从最新主分支创建功能分支：`git checkout -b feature/your-feature`；
2. 保持改动聚焦，一次 PR 只做一件事；
3. 提交前确保：
   - 固件可通过 `pio run` 编译（无新增编译错误）；
   - Python 脚本可通过 `python -m py_compile <file>` 语法检查；
4. 在 PR 描述中说明：**改了什么、为什么改、如何验证**（若有硬件验证请附串口日志）。

### 12.2 编码约定

- **命名**：模块内静态变量/函数用 `snake_case`，宏用全大写，类型用 `_t` 结尾；
- **日志**：统一使用 `LOG_ERR / LOG_WARN / LOG_INFO / LOG_DEBUG`，不要直接调用 `USBSerial.print*`；
- **字符串**：涉及用户输入或外部数据的字符串拷贝必须保证空终止（`strncpy` 后补 `'\0'`）；
- **内存**：避免在全局作用域 `new`（须在 `setup()` 内分配）；优先使用固定缓冲区，谨慎动态分配；
- **文件编码**：源码统一使用 **UTF-8（无 BOM）**，换行符遵循仓库现状；
- **头文件**：统一使用 `#pragma once` 或传统 include guard，二者勿混用同一头文件。

### 12.3 新增页面 / 服务

- 新增 UI 页面：在 `ui_pages.h` 的 `PageIndex` 枚举中追加，切勿使用魔法数字索引；
- 新增后台服务：放入 `src/service/`，并在 `main.cpp` 的 `setup_modules()` 中显式初始化。

---

## 13. 许可证

本项目采用 **MIT License**，完整条款见仓库根目录的 [`LICENSE`](LICENSE) 文件。

```
Copyright (c) 2026 Fenghu146
```

简而言之：你可以自由地使用、修改、分发本项目（包括商业用途），只需保留原始版权声明与许可证文本；
本软件按「原样」提供，作者不承担任何明示或默示的担保责任。

### 13.1 第三方组件

仓库 `lib/` 下随源码分发的第三方库各自遵循其原始许可证，均与 MIT 兼容：

| 组件 | 位置 | 许可证 | 版权 |
| ---- | ---- | ------ | ---- |
| GFX Library for Arduino | `lib/GFX_Library_for_Arduino` | MIT | Moon On Our Nation |
| SensorLib（Waveshare 版） | `lib/SensorLib-Waveshare` | MIT | lewis he |
| XPowersLib | `lib/XPowersLib` | MIT | lewis he |

部分字库文件（`lib/GFX_Library_for_Arduino/src/font/u8g2_font_*`）来自 u8g2，
遵循 **SIL Open Font License 1.1** 与 **GPLv2+（含字体嵌入例外）**，再分发时请保留其原始声明。

此外，经由 PlatformIO 拉取的依赖（`lvgl`、`CST816S`、`ArduinoJson`）同样采用 MIT 许可证。

---

## 14. 相关文档

| 文档 | 内容 |
| ---- | ---- |
| [`CLAUDE.md`](CLAUDE.md) | 硬件规格速查、当前状态、常见坑与交接流程 |
| [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) | 完整开发计划、里程碑与路线图 |
| [`EDGE_AI_TRAINING_PLAN.md`](EDGE_AI_TRAINING_PLAN.md) | 边缘 AI 训练方案与内存预算分析 |
| [`DEBUG_REPORT.md`](DEBUG_REPORT.md) | 历次故障排查记录（白屏、flash 损坏、LVGL、音频等） |
| [`WORK_REPORT.md`](WORK_REPORT.md) | 开发工作报告与优化记录 |
