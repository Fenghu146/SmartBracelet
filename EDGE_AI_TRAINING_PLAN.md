# SmartBracelet 边缘 AI 训练方案

> 基于 ESP32-S3 + QMI8658 IMU 的端侧机器学习全流程

---

## 目录

1. [硬件能力概览](#1-硬件能力概览)
2. [数据采集基础设施](#2-数据采集基础设施)
3. [数据集构建与预处理](#3-数据集构建与预处理)
4. [模型训练](#4-模型训练)
5. [模型导出到 TFLite](#5-模型导出到-tflite)
6. [设备端推理集成](#6-设备端推理集成)
7. [任务优先级路线图](#7-任务优先级路线图)
8. [附录：工具链安装](#8-附录工具链安装)

---

## 1. 硬件能力概览

| 项目     | 规格                                         |
| ------ | ------------------------------------------ |
| 主控     | ESP32-S3 (240MHz LX7 双核, **支持 PIE 向量指令集**) |
| PSRAM  | 8MB OPI — 可承载 ~2~3MB 模型                    |
| Flash  | 16MB — 当前用掉 1.6MB，空余 ~14MB                 |
| RAM 余量 | 当前 148KB/320KB (45%)，剩 ~170KB              |
| 传感器    | QMI8658 6轴 IMU (加速度计 1000Hz, 陀螺仪 897Hz)    |
| 关键特性   | HW FIFO (128 样本), 硬件计步器/双击/运动检测            |
| 开发环境   | PyTorch 2.11 + CUDA 128 已安装，需补充 TensorFlow |

### AI 加速能力

ESP32-S3 提供 **PIE 向量指令集**：

- 单周期 MAC 操作
- 8/16-bit 定点加速
- SIMD 向量处理
- 配合 **ESP-NN** 库获得接近 DSP 的推理性能

### 内存预算分析

| 组件                  | RAM        | Flash     |
| ------------------- | ---------- | --------- |
| 模型 (INT8, ~60KB)    | —          | 60KB      |
| Tensor Arena        | 100KB      | —         |
| 环形缓冲区 (50×6 floats) | 1.2KB      | —         |
| 推理中间变量              | ~10KB      | —         |
| **AI 总计**           | **~115KB** | **~60KB** |
| 当前空闲                | ~170KB     | ~14MB     |

> **关键瓶颈是 RAM** — Tensor Arena 建议放 PSRAM。

---

## 2. 数据采集基础设施

### 2.1 固件端：USB-CSV 转储模式

在 `main.cpp` 中增加条件编译的数据采集模式（通过串口命令触发）：

```cpp
// 伪代码 - 在 IMU 就绪回调中
if (data_collection_mode) {
    USBSerial.printf("%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s\n",
        millis(),
        acc.x, acc.y, acc.z,
        gyr.x, gyr.y, gyr.z,
        gesture_label  // 从串口输入设定
    );
}
```

| 方面      | 推荐方案                                     | 原因                                   |
| ------- | ---------------------------------------- | ------------------------------------ |
| 传输方式    | USB CDC (USBSerial) @ 921600             | 已有且稳定，带宽够 1000Hz × 6 floats ≈ 57KB/s |
| 数据格式    | CSV: `timestamp,ax,ay,az,gx,gy,gz,label` | Python pandas 直接读                    |
| 标注方式    | 串口输入数字编号 (如 `1`=走路, `2`=跑步)              | 实时标注，无需后期对齐                          |
| 采样率     | 降采样到 **50Hz** (每 N 帧取 1 帧)               | 大部分活动识别 20-50Hz 足够                   |
| FIFO 利用 | 使能 HW FIFO 批量读取 (每次 32 样本)               | 减少 I2C 开销                            |

### 2.2 Python 端：采集脚本

```python
# collect_data.py
import serial
import csv
import datetime

ser = serial.Serial('COM9', 921200, timeout=1)
label_map = {'1': 'walk', '2': 'run', '3': 'wave', '4': 'idle',
             '5': 'flick', '6': 'circle', '7': 'sit', '8': 'fall'}

filename = f"imu_data_{datetime.datetime.now():%Y%m%d_%H%M%S}.csv"
current_label = "idle"

with open(filename, 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['timestamp', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'label'])

    while True:
        if ser.in_waiting:
            line = ser.readline().decode().strip()
            if line in label_map:
                current_label = label_map[line]
                print(f"Label set to: {current_label}")
            elif line.startswith("DATA,"):
                data = line.split(',')[1:]
                writer.writerow(data + [current_label])
```

---

## 3. 数据集构建与预处理

### 3.1 滑动窗口

IMU 时间序列 → 固定长度窗口：

```python
def create_windows(data, window_size=50, stride=25):
    """
    IMU 50Hz, window_size=50 → 1秒窗口, stride=25 → 50% 重叠
    """
    X, y = [], []
    for i in range(0, len(data) - window_size, stride):
        window = data[i:i+window_size, 0:6]
        label = mode(data[i:i+window_size, 6])
        X.append(window)
        y.append(label)
    return np.array(X), np.array(y)
```

| 任务           | 窗口长度       | 步长         | 传感器       |
| ------------ | ---------- | ---------- | --------- |
| 活动识别 (走/跑/坐) | 2秒 (100帧)  | 1秒 (50帧)   | acc + gyr |
| 手势识别 (挥手/画圈) | 1秒 (50帧)   | 0.5秒 (25帧) | acc + gyr |
| 跌倒检测         | 3秒 (150帧)  | —          | acc 原始高G  |
| 计步改进         | 0.5秒 (25帧) | 实时滑动       | acc 幅值    |

### 3.2 数据增强

```python
def augment_window(window):
    # 1. 高斯噪声
    noise = np.random.normal(0, 0.01, window.shape)
    # 2. 时间扭曲 (随机缩放)
    scale = np.random.uniform(0.9, 1.1)
    # 3. 轴旋转 (绕重力方向小角度)
    angle = np.random.uniform(-5, 5)
    return window + noise
```

---

## 4. 模型训练

### 4.1 推荐模型架构

#### 方案 A：轻量 1D-CNN（推荐首选）

参数量 ~15K，模型大小 ~60KB INT8，推理时间 ~5ms：

```python
import torch
import torch.nn as nn

class TinyHAR(nn.Module):
    def __init__(self, n_channels=6, n_classes=4):
        super().__init__()
        self.conv1 = nn.Conv1d(n_channels, 16, kernel_size=7, padding='same')
        self.bn1 = nn.BatchNorm1d(16)
        self.conv2 = nn.Conv1d(16, 32, kernel_size=5, padding='same')
        self.bn2 = nn.BatchNorm1d(32)
        self.conv3 = nn.Conv1d(32, 64, kernel_size=3, padding='same')
        self.bn3 = nn.BatchNorm1d(64)
        self.pool = nn.AdaptiveAvgPool1d(1)
        self.fc = nn.Linear(64, n_classes)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = torch.relu(self.bn1(self.conv1(x)))
        x = torch.relu(self.bn2(self.conv2(x)))
        x = torch.relu(self.bn3(self.conv3(x)))
        x = self.pool(x).squeeze(-1)
        return self.fc(x)
```

#### 方案 B：TCN（更高精度）

参数量 ~25K，模型大小 ~100KB INT8：

```python
class TinyTCN(nn.Module):
    def __init__(self, n_channels=6, n_classes=4):
        super().__init__()
        self.conv1 = nn.Conv1d(n_channels, 16, 3, dilation=1, padding=1)
        self.conv2 = nn.Conv1d(16, 32, 3, dilation=2, padding=2)
        self.conv3 = nn.Conv1d(32, 64, 3, dilation=4, padding=4)
        self.pool = nn.AdaptiveAvgPool1d(1)
        self.fc = nn.Linear(64, n_classes)
```

### 4.2 训练流程

```python
# train.py
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

X_train, y_train = load_windows("imu_data_walk_run_idle.csv")
X_train = torch.FloatTensor(X_train)
y_train = torch.LongTensor(y_train)
dataset = TensorDataset(X_train, y_train)
loader = DataLoader(dataset, batch_size=32, shuffle=True)

model = TinyHAR(n_classes=4)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

for epoch in range(50):
    for X_batch, y_batch in loader:
        optimizer.zero_grad()
        outputs = model(X_batch)
        loss = criterion(outputs, y_batch)
        loss.backward()
        optimizer.step()
    acc = evaluate(model, X_val, y_val)
    print(f"Epoch {epoch}: val_acc = {acc:.3f}")
```

### 4.3 预期准确率

| 任务              | 类数  | 预期准确率 | 模型大小 (INT8) | 推理时间  |
| --------------- | --- | ----- | ----------- | ----- |
| 4 活动 (走/跑/坐/静止) | 4   | 95%+  | ~60KB       | 3-5ms |
| 手势 (挥手/画圈/双击/无) | 4   | 90%+  | ~60KB       | 3-5ms |
| 跌倒检测            | 2   | 97%+  | ~30KB       | 2-3ms |

---

## 5. 模型导出到 TFLite

```python
# convert.py
import torch
import onnx
import tensorflow as tf

# PyTorch → ONNX
torch.onnx.export(model, dummy_input, "model.onnx",
                  input_names=['input'], output_names=['output'],
                  dynamic_axes={'input': {0: 'batch', 1: 'window'}})

# ONNX → TensorFlow
import onnx_tf
tf_model = onnx_tf.backend.prepare(onnx.load("model.onnx"))
tf_model.export_graph("model_savedmodel")

# TensorFlow → TFLite (INT8 量化)
converter = tf.lite.TFLiteConverter.from_saved_model("model_savedmodel")

def representative_dataset():
    for i in range(100):
        yield [X_calib[i:i+1].numpy().astype(np.float32)]

converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()
with open("model_int8.tflite", "wb") as f:
    f.write(tflite_model)

print(f"Model size: {len(tflite_model) / 1024:.1f} KB")
```

**转换路线图：**

```
PyTorch (训练) → ONNX (桥接) → TensorFlow SavedModel → TFLite (INT8量化)
```

> **为什么不直接 PyTorch → TFLite？** TFLite 的生态最成熟，ESP32-S3 有专门的 `tflite-micro` 支持和 ESP-NN 加速。

### 模型转 C 数组

```python
# model_to_c_array.py
with open("model_int8.tflite", "rb") as f:
    data = f.read()
    print(f"const unsigned char model_data[] = {{")
    for i, b in enumerate(data):
        print(f"0x{b:02x},", end=" " if (i+1) % 12 else "\n")
    print(f"\n}};")
    print(f"const unsigned int model_len = {len(data)};")
```

输出放到 `src/model.h`。

---

## 6. 设备端推理集成

### 6.1 PlatformIO 配置

```ini
[env:esp32s3]
platform = espressif32@6.9.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_mode = qio
board_build.psram = enable
board_build.psram_mode = opi
board_build.psram_frequency = 80m
lib_deps =
    fbiego/CST816S
    lvgl/lvgl@^8.4.0
    tflite-micro[tflm];esp32
lib_extra_dirs = lib
build_flags =
    -DBOARD_HAS_PSRAM
    -DCONFIG_SPIRAM_MODE_OCT
    -mfix-esp32-psram-cache-issue
```

### 6.2 推理引擎初始化

```cpp
#include <TensorFlowLite_ESP32.h>
#include "model.h"

static const int tensor_arena_size = 100 * 1024;
static uint8_t tensor_arena[tensor_arena_size];

static tflite::MicroErrorReporter micro_error;
static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input = nullptr, *output = nullptr;

void ai_init() {
    static tflite::Model* model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) return;

    static tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddConv2D();
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, tensor_arena_size, &micro_error);
    interpreter = &static_interpreter;

    input = interpreter->input(0);
    output = interpreter->output(0);
}
```

### 6.3 推理调用

```cpp
int ai_predict(const float window[50][6]) {
    for (int i = 0; i < 50 * 6; i++) {
        float val = ((float*)window)[i];
        input->data.int8[i] = (int8_t)(
            val / input->params.scale + input->params.zero_point);
    }

    interpreter->Invoke();

    int best_class = 0;
    int8_t best_score = output->data.int8[0];
    for (int c = 1; c < output->dims->data[1]; c++) {
        if (output->data.int8[c] > best_score) {
            best_score = output->data.int8[c];
            best_class = c;
        }
    }
    return best_class;
}
```

### 6.4 环形缓冲区（滑动窗口）

```cpp
class IMUBuffer {
    static const int WINDOW = 50;
    static const int STRIDE = 25;
    float buf[WINDOW][6];
    int head = 0;
    int count = 0;

public:
    void push(float ax, float ay, float az,
              float gx, float gy, float gz) {
        buf[head][0] = ax; buf[head][1] = ay; buf[head][2] = az;
        buf[head][3] = gx; buf[head][4] = gy; buf[head][5] = gz;
        head = (head + 1) % WINDOW;
        if (count < WINDOW) count++;
    }

    bool ready() { return count >= WINDOW; }

    float* get_window() {
        static float win[WINDOW][6];
        for (int i = 0; i < WINDOW; i++) {
            int idx = (head + i) % WINDOW;
            memcpy(win[i], buf[idx], 6 * sizeof(float));
        }
        return (float*)win;
    }
};
```

---

## 7. 任务优先级路线图

### 时间线

```
第1周 ────── 第2周 ────── 第3周 ────── 第4周
┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐
│ 数据采集     │ │ 模型训练     │ │ TFLite 集成 │ │ 优化 + 测试  │
│            │ │            │ │            │ │            │
│ 固件CSV模式  │ │ PyTorch    │ │ 推理引擎     │ │ ESP-NN     │
│ 采集脚本     │ │ 1D-CNN     │ │ 模型嵌入     │ │ 功耗测量     │
│ 数据集标注   │ │ ONNX→TFL   │ │ 环形缓冲区   │ │ 混淆矩阵     │
│ 数据增强     │ │ INT8量化    │ │ 结果后处理   │ │ 实测验证     │
└────────────┘ └────────────┘ └────────────┘ └────────────┘
```

### 并行工作流

- **轨道 A（固件）**：周1 数据采集模式 → 周3 环形缓冲区 → 周末 TFLite 集成
- **轨道 B（Python）**：周1 采集 → 周2 训练 → 周3 导出 → 周4 微调

### 第一迭代推荐

| 任务              | 类   | 优先级 | 难度  | 理由              |
| --------------- | --- | --- | --- | --------------- |
| 活动分类 (走/跑/坐/静止) | 4   | P0  | ⭐⭐  | 数据好标，模型小，效果立竿见影 |
| 手势控制 (翻腕/甩腕)    | 2-3 | P1  | ⭐⭐⭐ | 替代当前阈值抬手亮屏      |
| 跌倒检测 (跌倒/正常)    | 2   | P1  | ⭐⭐  | 安全功能，二分类简单      |
| 计步改进            | 回归  | P2  | ⭐⭐⭐ | 当前启发式算法已经可用     |

> **强烈建议从活动分类开始** — 只需 10-15 分钟标注数据就能训练，体验完整采集→训练→部署闭环。

---

## 8. 附录：工具链安装

```bash
# 需要补充的 Python 包
pip install tensorflow
pip install onnx onnxruntime
pip install onnx-tf
pip install pyserial
```

### 参考项目

| 项目                         | 链接                                                               | 参考价值                     |
| -------------------------- | ---------------------------------------------------------------- | ------------------------ |
| TFLiteMicro_ArduinoESP32S3 | https://github.com/j-siderius/TFLiteMicro_ArduinoESP32S3         | TFLite Micro + ESP-NN 集成 |
| esp32pico_watch            | https://github.com/dht3218/esp32pico_watch                       | MFCC+CNN 端侧部署            |
| xiaozhi-esp32              | https://github.com/dotnfc/xiaozhi-esp32/tree/wristgem            | AI 语音架构参考                |
| TensorFlow HAR 教程          | https://www.tensorflow.org/tutorials/structured_data/time_series | 时序分类入门                   |
