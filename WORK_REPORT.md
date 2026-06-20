# SmartBracelet 项目开发工作报告

> 基于 ESP32-S3-Touch-LCD-1.83 智能手表固件的完整开发、优化与集成记录

---

## 一、项目概况

| 项目 | 说明 |
|------|------|
| 硬件平台 | Waveshare ESP32-S3-Touch-LCD-1.83（ESP32-S3-R8-OPI） |
| 主控 | ESP32-S3 双核 240MHz，8MB OPI PSRAM |
| 显示屏 | 1.83寸 IPS TFT 240×280，ST7789 SPI 驱动 |
| 触摸 | CST816S I2C 触摸控制器 |
| 音频 | ES8311 I2S 编解码器 + INMP441 MEMS 麦克风 + PCA9557 PA控制 |
| 电源 | AXP2101 PMU（I2C），锂电池充放电管理 |
| 传感器 | QMI8658 六轴 IMU（加速度+陀螺仪），PCF85063 RTC |
| 无线 | Wi-Fi 802.11 b/g/n + BLE 5.0（NimBLE） |
| 开发框架 | Arduino + PlatformIO + FreeRTOS + LVGL 8.x |
| 手机端 | Capacitor + Web Bluetooth 混合 App |

### 编译指标（最终版本）

| 指标 | 数值 |
|------|------|
| RAM 占用 | 47.1%（154,380 / 327,680 bytes） |
| Flash 占用 | 62.6%（2,092,717 / 3,342,336 bytes） |
| 编译耗时 | ~70s（增量）/ ~300s（全量） |

---

## 二、CodeAtlas 全面优化（共 16 项）

### 2.1 安全与健壮性修复（4 项）

| # | 文件 | 问题 | 修复方案 |
|---|------|------|----------|
| 1 | `service/ble_srv.cpp` | `strncpy` 后未保证空终止，可能导致缓冲区溢出 | 每个 `strncpy` 后强制 `buf[max-1] = '\0'` |
| 2 | `main.cpp` | `serial_buf` 溢出时 `serial_len++` 可能越界 | 添加 `serial_len >= 255` 时自动重置逻辑 |
| 3 | `service/audio.cpp` | `audio_play_sine` 一次性 malloc 264KB 缓冲区 | 改为 1024 samples 分块生成+写入，峰值内存降至 ~2KB |
| 4 | `service/audio.cpp` | `play_wav_task` 多条错误退出路径未关闭文件/重置状态 | 统一错误处理，确保所有退出路径释放资源 |

### 2.2 性能与内存优化（5 项）

| # | 文件 | 问题 | 修复方案 |
|---|------|------|----------|
| 5 | `ui_pages.cpp` | 通知页面每次 tick 全部 `lv_obj_clean` 再重建 | 维护 `last_notif_count` 计数器，仅在数量变化时重建 |
| 6 | `settings_page.cpp` | 每次 update 从 NVS（Flash）读取 5 个 key | 添加静态缓存变量，仅在值变更时更新 UI |
| 7 | `main.cpp` | `fill_telemetry` + `push_ble_telemetry` 重复读取 PMU 寄存器 | `push_ble_telemetry` 复用 `telem->batt_mv` / `telem->charging` 缓存值 |
| 8 | `main.cpp` | 充电状态判断逻辑在 2 处重复 | 提取 `is_charging()` 辅助函数 |
| 9 | `quick_panel.cpp` | 按钮颜色切换逻辑在 3 处重复 | 提取 `update_toggle_style(btn, lbl, on)` 辅助函数 |

### 2.3 代码质量重构（4 项）

| # | 文件 | 问题 | 修复方案 |
|---|------|------|----------|
| 10 | `ui_pages.h` + `main.cpp` | 页面索引使用魔数（如 `pages[3]`） | 定义 `enum PageIndex { PAGE_DIGITAL=0, ... PAGE_COUNT }` |
| 11 | `src/pin_config.h` | 与 `include/pin_config.h` 重复 | 删除 `src/pin_config.h`，统一使用 `include/pin_config.h` |
| 12 | `main.cpp` | `loop_communication()` 含 5 个关注点 | 提取 `loop_serial_bridge()` 函数 |
| 13 | `service/audio.cpp` | 混用 `USBSerial.println` 和 LOG 宏 | 全部替换为 `LOG_INFO/LOG_ERR/LOG_WARN`，添加 `#include "../debug_log.h"` |

### 2.4 小优化轮次（4 项）

| # | 文件 | 修复 |
|---|------|------|
| 14 | `main.cpp` | `handle_fall_alert` 的 3 处 `strncpy` 添加空终止 |
| 15 | `main.cpp` | `fill_telemetry` 消除双重 PMU I2C 读取（`read_batt_voltage_raw` 只读一次） |
| 16 | `service/ota_update.cpp` | OTA 下载缓冲区从 4096B 缩至 1024B，节省 3KB 任务栈空间 |
| 17 | `player.cpp` | `player_update` 改为仅在播放状态变化时更新 UI 标签 |

---

## 三、Bug 修复（3 项）

### 3.1 通知消息重叠显示

**根因**：通知卡片 `card` 对象使用 LVGL 默认绝对定位布局，子元素（header/title/body）全部堆叠在 (0,0) 位置。

**修复**（`ui_pages.cpp`）：
```c
lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
lv_obj_set_style_pad_row(card, 2, 0);
```
同时为所有 label 设置 `lv_obj_set_width(label, LV_PCT(100))`，确保 `LV_LABEL_LONG_DOT` 截断模式生效。

### 3.2 天气无法显示

**根因**：手机 App 通过 `navigator.geolocation.getCurrentPosition` 发送 GPS 定位给手表，但 GPS 获取可能在室内超时或权限被拒后静默失败，无重试机制。

**修复**（`webapp/www/js/app.js`）：
- 添加 1 次自动重试（GPS 失败 5s 后重试，BLE 写入失败 3s 后重试）
- 超时时间从 10s 增至 15s
- 添加 `enableHighAccuracy: false` 降低精度要求
- 添加 `locationSent` 标志防止重复发送

### 3.3 BLE 连接后 App 界面无法进入

**根因**：Web Bluetooth GATT 操作（`getPrimaryService`/`getCharacteristic`/`startNotifications`）可能无限挂起，`_connectWebBluetooth()` 无超时保护。

**修复**（`webapp/www/js/ble.js` + `app.js`）：
- 添加 `_timeout(promise, ms, label)` 辅助方法
- 为每个 GATT 操作添加 5-10s 超时保护
- 为整个连接流程添加 45s 总超时
- 添加进度回调，实时显示连接阶段（Connecting → Discovering → Subscribing → Syncing）
- `readAll()` 失败不阻止进入 Dashboard

---

## 四、功能裁剪（3 项）

### 4.1 移除睡眠追踪模块

**删除文件**：`sleep_tracker.cpp`、`sleep_tracker.h`

**移除引用**：
- `ui_pages.h`：移除 `sleeping`、`sleep_total_min`、`sleep_deep_min` 遥测字段
- `ui_pages.cpp`：移除 `sleep_label` 创建（6行）和更新逻辑（10行）
- `main.cpp`：移除 `#include`、`sleep_tracker_init()`、`sleep_tracker_update()`、`sleep_tracker_reset()`

**效果**：RAM -40B，Flash -1KB，传感器页面从 6 行数据变为 5 行。

### 4.2 移除 Activity AI 模块

**删除文件**：`activity.cpp`、`activity.h`、`activity_model.h`

**移除引用**：
- `main.cpp`：`#include`、`current_activity` 变量、`PAGE_ACTIVITY` 更新、BLE 活动推送、IMU 特征上传、`IMU_FEATURE_INTERVAL_MS` 宏
- `ui_pages.cpp/h`：`#include`、activity 页面创建、`PAGE_ACTIVITY` 枚举值
- `sensor_task.cpp`：`#include`、`activity_push_data()` 调用

**效果**：RAM 47.5% → 47.1%（-1.3KB），Flash 62.7% → 62.6%（-2.7KB），页面数 11 → 10。

### 4.3 移除启动蓝屏

**修复**（`main.cpp`）：
```c
// 之前：gfx->fillScreen(DISPLAY_TEST_BLUE);  // 0x001F 蓝色
// 之后：gfx->fillScreen(0x0000);              // 黑色，开机后直接显示 LVGL 表盘
```

---

## 五、小智AI语音助手集成

### 5.1 架构决策

采用**手机桥接方案**：
```
[手表] <--BLE--> [手机App] <--WebSocket--> [小智AI服务器]
  显示结果        语音识别+通信              AI处理+TTS
```

选择此方案的原因：
- 避免在 ESP32 上实现 OPUS 编解码（内存开销大）
- 利用手机浏览器 Web Speech API 做 STT
- 手表已有 BLE voice_chat 通道，无需固件改动

### 5.2 新增文件

| 文件 | 说明 |
|------|------|
| `webapp/www/js/xiaozhi.js` | XiaozhiClient WebSocket 客户端类（347行） |
| `webapp/www/js/chat.js` | 重写为小智协议集成（替代 DeepSeek 文本聊天） |
| `webapp/www/index.html` | Chat 卡片改为小智 AI 配置界面 |

### 5.3 协议实现

小智 WebSocket 协议关键消息：
```javascript
// 客户端 hello
{type:"hello", version:2, transport:"websocket",
 audio_params:{format:"opus",sample_rate:16000,channels:1,frame_duration:60}}

// 开始监听
{session_id:"xxx", type:"listen", state:"start", mode:"auto"}

// 停止监听
{session_id:"xxx", type:"listen", state:"stop"}
```

服务器响应类型：`hello`（session_id）、`stt`（语音转文字）、`tts`（音频状态）、二进制 OPUS 音频帧。

### 5.4 使用方法

1. 访问 xiaozhi.me 注册账号，获取 WebSocket URL 和 Token
2. 手机 App 填入配置，点击 Connect
3. 语音输入 → Web Speech API → 文本 → WebSocket → AI 回复 → BLE 推送到手表

---

## 六、竞赛报告对比分析

### 6.1 报告与实际代码的差异

| 报告说法 | 实际代码 |
|----------|----------|
| "ESP-IDF v5.x 框架" | **Arduino 框架** + PlatformIO |
| "1.69寸 IPS 240×280" | 硬件标注 **1.83寸**，分辨率 240×280 |
| "NS4150 功放" | **ES8311 音频编解码器** + PA |
| "PDM/I2S 麦克风" | **I2S 标准模式** INMP441（非 PDM） |
| "三个按键导航" | 主要靠 **触摸屏手势**，物理按键仅 BOOT/RST |

### 6.2 报告未提及但已实现的功能

以下功能建议在报告中补充：

- 触摸交互系统（CST816S 手势：滑动翻页、长按切表盘、下拉快捷面板）
- BLE 完整服务架构（通知推送、OTA升级、HID音乐控制、数据遥测）
- Wi-Fi + NTP 自动校时
- 步数计数器 + NVS 持久化 + 崩溃恢复
- 跌倒检测（加速度三段判定：自由落体→撞击→静止）
- 运动强度估算（METs + 卡路里消耗）
- 天气查询（open-meteo API + 手机GPS定位同步）
- 秒表、TF卡音频播放器
- 手机 App（Capacitor + Web Bluetooth 混合应用）
- 小智AI语音助手桥接

### 6.3 报告提到可进一步改进的代码方向

| 报告建议 | 当前状态 | 改进方向 |
|----------|----------|----------|
| 按键 GPIO 中断+消抖 | 触摸轮询，BOOT 键未使用 | 可接入 BOOT 键中断作为快捷操作 |
| AXP2101 多路独立电源域管理 | 有 deep sleep，缺少运行时动态关域 | 可添加运动/夜间模式关闭特定电源域 |
| 音频环形缓冲区 | FreeRTOS Queue 传 chunk | 可改用标准 RingBuffer 提升效率 |
| LVGL 帧率 ≥30fps 验证 | 未做帧率统计 | 可启用 `LV_USE_PERF_MONITOR` |

---

## 七、关键文件索引

### 固件源码（src/）

| 文件 | 功能 |
|------|------|
| `main.cpp` | 主入口：setup/loop、页面管理、遥测、电源管理 |
| `ui_pages.cpp/h` | UI 页面创建与更新（表盘/传感器/通知/天气/播放器/语音/音乐） |
| `ui_styles.cpp/h` | 全局 LVGL 样式定义 |
| `watch_faces.cpp/h` | 表盘管理（数字/模拟/运动） |
| `settings_page.cpp/h` | 设置页面（步数目标/亮度/DND/表盘选择） |
| `quick_panel.cpp/h` | 下拉快捷面板（WiFi/BLE/DND/亮度） |
| `sensor_task.cpp/h` | IMU 传感器 FreeRTOS 任务（Core 0，125Hz） |
| `step_counter.cpp/h` | 步数计数器 |
| `fall_detect.cpp/h` | 跌倒检测（三段加速度判定） |
| `motion_intensity.cpp/h` | 运动强度/METs/卡路里估算 |
| `wrist_detect.cpp/h` | 抬手检测 |
| `nvs_store.cpp/h` | NVS 持久化存储（Preferences） |
| `notif_history.cpp/h` | 通知历史环形缓冲区 |
| `batt_health.cpp/h` | 电池健康追踪 |
| `backlight.cpp/h` | 背光 PWM 控制 |
| `stopwatch.cpp/h` | 秒表功能 |
| `weather.cpp/h` | 天气查询（open-meteo API） |
| `player.cpp/h` | TF 卡 WAV 播放器 |
| `service/audio.cpp/h` | ES8311 + I2S 音频驱动（TX/RX） |
| `service/ble_srv.cpp/h` | BLE GATT 服务（通知/数据/OTA/HID） |
| `service/ble_hid.cpp/h` | BLE HID 音乐控制 |
| `service/ota_update.cpp/h` | OTA 固件升级（WiFi HTTP + BLE） |
| `service/wifi_ntp.cpp/h` | Wi-Fi 连接 + NTP 校时 |
| `service/voice_chat.cpp/h` | BLE 语音聊天服务 |
| `service/tf_card.cpp/h` | TF 卡 SD_MMC 驱动 |
| `voice_chat_ui.cpp/h` | 语音聊天 LVGL 页面 |
| `debug_log.h` | 分级日志宏（ERR/WARN/INFO/DEBUG/VERBOSE） |

### 手机 App（webapp/www/）

| 文件 | 功能 |
|------|------|
| `index.html` | 主页面（连接/仪表盘/通知/聊天/OTA/历史） |
| `js/app.js` | App 控制器（扫描/连接/渲染/BLE回调） |
| `js/ble.js` | BLE 服务类（Web Bluetooth + Capacitor 双平台） |
| `js/xiaozhi.js` | 小智AI WebSocket 客户端 |
| `js/chat.js` | 聊天模块（小智协议集成） |
| `js/charts.js` | 历史数据图表（Chart.js） |
| `js/ota.js` | BLE OTA 升级 |
| `css/style.css` | 全局样式 |

### 构建配置

| 文件 | 说明 |
|------|------|
| `platformio.ini` | PlatformIO 构建配置（板型/编译宏/库依赖） |
| `boards/ESP32-S3-R8-OPI.json` | 自定义板型定义 |
| `include/lv_conf.h` | LVGL 配置（颜色深度/字体/组件） |
| `include/pin_config.h` | 引脚定义（唯一版本） |

---

## 八、已知问题与注意事项

1. **PlatformIO 命令**：Windows PowerShell 中需确保 PATH 包含 `~/.platformio/penv/Scripts`
2. **USB CDC**：ESP32-S3 原生 USB 端口，串口类为 `Serial`（非 `USBSerial`），已通过 `LOG_SERIAL` 宏统一
3. **LVGL 显示缓冲**：必须禁用 PSRAM 分配（`LV_MEM_CUSTOM=0`），使用内部 SRAM
4. **`Update.write()` 参数**：需要 `(uint8_t*)` 强制类型转换
5. **多文件结构体重定义**：Arduino 多 .cpp 文件包含同一头文件时，结构体需放在 `.h` 中并使用 `#pragma once`
6. **Web Bluetooth**：浏览器 WebSocket 无法设置自定义 Header，Token 通过 hello 消息传递

---

## 九、版本历史

| 版本 | 主要变更 |
|------|----------|
| 1.0.0 | 基础功能：表盘/传感器/通知/天气/秒表/播放器/语音/音乐/设置 |
| 1.1.0 | CodeAtlas 优化：安全修复 + 性能优化 + 代码重构（16项） |
| 1.2.0 | Bug 修复：通知重叠 + 天气定位 + BLE 连接超时 |
| 1.3.0 | 功能裁剪：移除睡眠追踪 + Activity AI + 启动蓝屏 |
| 1.4.0 | 小智AI集成：手机桥接 WebSocket 语音助手 |
