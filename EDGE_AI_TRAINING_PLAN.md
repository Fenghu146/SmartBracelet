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
8. [最小 Demo：30 分钟跑通闭环](#8-最小-demo30-分钟跑通闭环)
9. [附录：工具链安装](#9-附录工具链安装)

---

## 1. 硬件能力概览

| 项目     | 规格                                         |
| ------ | ------------------------------------------ |
| 主控     | ESP32-S3 (240MHz LX7 双核, **支持 PIE 向量指令集**) |
| PSRAM  | **不可用**（`BOARD_HAS_PSRAM` 注释掉，初始化失败）      |
| Flash  | 16MB — 当前用掉 1.8MB，空余 ~13MB                 |
| RAM 余量 | 当前 149KB/320KB (45.6%)，剩 ~170KB             |
| 传感器    | QMI8658 6轴 IMU (加速度计 1000Hz, 陀螺仪 897Hz)    |
| 关键特性   | HW FIFO (128 样本), 硬件计步器/双击/运动检测            |
| 开发环境   | PyTorch 2.11 + CUDA 128 已安装                 |

### ⚠️ PSRAM 不可用 — 关键约束

计划中 Tensor Arena 原定 100KB 放 PSRAM，但此板 PSRAM 初始化失败。100KB arena 在 DRAM 中占剩余 170KB 的 **60%**，加上 LVGL heap (64KB) 和 BLE/WiFi 栈 (~30KB) 后相当紧张。

**应对策略**：
- 模型尽量小（< 30KB），arena 降至 **60KB**
- 或先不做 TFLite，改用 **决策树 C 数组导出**（零 arena 开销）

### 内存预算分析（PSRAM 不可用版）

| 组件                  | RAM        | Flash     |
| ------------------- | ---------- | --------- |
| 模型 (INT8, ~30KB)    | —          | 30KB      |
| Tensor Arena        | 60KB       | —         |
| 环形缓冲区 (50×6 float) | 1.2KB      | —         |
| 推理中间变量              | ~5KB       | —         |
| **AI 总计**           | **~66KB**  | **~30KB** |
| 当前空闲                | ~170KB     | ~13MB     |

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
| 传输方式    | USB CDC (USBSerial) @ **115200**         | 921600 在此板不稳定，115200 足够 50Hz × 6 floats |
| 数据格式    | CSV: `timestamp,ax,ay,az,gx,gy,gz,label` | Python pandas 直接读                    |
| 标注方式    | **串口输入数字编号** 或 **触摸屏点选**              | 触摸屏更方便：显示活动列表，点即标                     |
| 采样率     | 降采样到 **50Hz** (每 N 帧取 1 帧)               | 大部分活动识别 20-50Hz 足够                   |

### 2.2 Python 端：采集脚本

```python
# collect_data.py — 115200 波特率
import serial, csv, datetime

ser = serial.Serial('COM9', 115200, timeout=1)
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

```python
def create_windows(data, window_size=50, stride=25):
    """50Hz → 1秒窗口, 50% 重叠"""
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
| 活动识别 (走/跑/静止) | 1秒 (50帧)   | 0.5秒 (25帧) | acc + gyr |
| 手势识别 (挥手/画圈)  | 1秒 (50帧)   | 0.5秒 (25帧) | acc + gyr |
| 跌倒检测         | 3秒 (150帧)  | —          | acc       |

---

## 4. 模型训练

### 4.1 推荐模型架构：超轻量 1D-CNN

参数量 ~5K，模型大小 **~20KB INT8**，推理时间 ~2ms：

```python
import torch
import torch.nn as nn

class TinyHAR(nn.Module):
    def __init__(self, n_channels=6, n_classes=3):  # walk/run/静止
        super().__init__()
        self.conv1 = nn.Conv1d(n_channels, 8, kernel_size=5, padding='same')
        self.bn1 = nn.BatchNorm1d(8)
        self.conv2 = nn.Conv1d(8, 16, kernel_size=3, padding='same')
        self.bn2 = nn.BatchNorm1d(16)
        self.pool = nn.AdaptiveAvgPool1d(1)
        self.fc = nn.Linear(16, n_classes)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = torch.relu(self.bn1(self.conv1(x)))
        x = torch.relu(self.bn2(self.conv2(x)))
        x = self.pool(x).squeeze(-1)
        return self.fc(x)
```

> 相比原版 3 层卷积（15K params），改为 2 层（5K params），INT8 后 ~20KB，arena 可降至 60KB。

### 4.2 训练流程

```python
# train.py
X_train, y_train = load_windows("imu_data_walk_run_idle.csv")
X_train = torch.FloatTensor(X_train)
y_train = torch.LongTensor(y_train)
dataset = TensorDataset(X_train, y_train)
loader = DataLoader(dataset, batch_size=32, shuffle=True)

model = TinyHAR(n_classes=3)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

for epoch in range(30):
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
| 3 活动 (走/跑/静止)  | 3   | 95%+  | ~20KB       | 2-3ms |
| 手势 (挥手/画圈/无)   | 3   | 90%+  | ~20KB       | 2-3ms |
| 跌倒检测            | 2   | 97%+  | ~15KB       | 1-2ms |

---

## 5. 模型导出到 TFLite

### 简化导出路线（无需 TensorFlow）

```
PyTorch → ONNX → onnx2tf → TFLite
```

```bash
pip install onnx onnxruntime onnx2tf
```

```python
# convert.py
import torch
import onnx

# PyTorch → ONNX
dummy = torch.randn(1, 50, 6)
torch.onnx.export(model, dummy, "model.onnx",
                  input_names=['input'], output_names=['output'])

# ONNX → TFLite (使用 onnx2tf)
import onnx2tf
onnx2tf.convert(
    input_model="model.onnx",
    output_folder_path="tflite_model",
    quant_static=True,
    calibration_data=X_calib_numpy  # INT8 量化校准数据
)
# 输出: tflite_model/model_float16.tflite
```

或者用 **ai_edge_torch**（Google 官方，直接 PyTorch → TFLite，跳过 ONNX）：

```bash
pip install ai-edge-torch
```

```python
import ai_edge_torch
import torch

# ai_edge_torch 直接 PyTorch → TFLite INT8
edge_model = ai_edge_torch.convert(model.eval(), (dummy,))
edge_model.export("model_int8.tflite")
```

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

### 6.1 PlatformIO 配置（无需 PSRAM 版）

```ini
[env:esp32s3]
lib_deps =
    fbiego/CST816S
    lvgl/lvgl@^8.4.0
    bblanchon/ArduinoJson @ ^7.0.0
    # TFLite Micro — 建议先不做，用最小 Demo 方案替代
```

### 6.2 推理引擎初始化（TFLite Micro 路线）

```cpp
#include <TensorFlowLite_ESP32.h>
#include "model.h"

static const int tensor_arena_size = 60 * 1024;  // 缩减至 60KB
static uint8_t tensor_arena[tensor_arena_size];

void ai_init() {
    static tflite::Model* model = tflite::GetModel(model_data);
    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddConv2D();
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected();
    static tflite::MicroInterpreter interpreter(
        model, resolver, tensor_arena, tensor_arena_size);
    interpreter->AllocateTensors();
}
```

### 6.3 环形缓冲区（滑动窗口）

沿用计划中的 `IMUBuffer` 类，WINDOW=50, STRIDE=25。

---

## 7. 任务优先级路线图

### 第一迭代推荐

| 任务              | 类   | 优先级 | 难度  | 理由              |
| --------------- | --- | --- | --- | --------------- |
| 活动分类 (走/跑/静止) | 3   | P0  | ⭐⭐  | 数据好标，模型小，效果立竿见影 |
| 手势控制 (翻腕/甩腕)  | 2   | P1  | ⭐⭐⭐ | 替代当前阈值抬手亮屏      |
| 跌倒检测           | 2   | P1  | ⭐⭐  | 安全功能            |
| 计步改进           | 回归  | P2  | ⭐⭐⭐ | 当前算法已可用         |

---

## 8. 最小 Demo：30 分钟跑通闭环

如果不想碰 TFLite Micro 集成，可以用**决策树 C 数组导出**方案——零额外库依赖，零 arena 开销。

### 方案：scikit-learn Random Forest → C 数组

```python
# 第1步：采集 3×30 秒数据（走、跑、静止）
#   用手表串口输出 CSV，Python 脚本保存
#   每条数据：ax,ay,az,gx,gy,gz,label

# 第2步：训练
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.tree import export_text

data = np.loadtxt("imu_data.csv", delimiter=",", skiprows=1)
X, y = data[:, :6], data[:, 6]
# 简单特征：每轴的均值和标准差（6轴 × 2特征 = 12维）
X_feat = np.column_stack([
    X.mean(axis=0),  # 6维
    X.std(axis=0),   # 6维
])

model = RandomForestClassifier(n_estimators=10, max_depth=4)
model.fit(X_feat, y)

# 第3步：导出为 C 数组
from sklearn.tree import DecisionTreeClassifier
trees = [TreePrinter(tree.tree_) for tree in model.estimators_]
# 输出 C 代码：每个决策树的节点阈值和左右分支
```

**固件端**（~50 行 C 代码，无任何外部库）：

```cpp
// 对每个决策树做推理，取多数投票
static float mean[6], std[6];  // 每轴均值、标准差
static int nb_classes = 3;

int predict(float features[6]) {
    // 计算均值和标准差 => 12维特征向量
    int votes[3] = {0};
    for (int t = 0; t < 10; t++) {       // 10棵树
        int node = 0;
        while (1) {
            int feat = tree_nodes[t][node].feat;
            float thr = tree_nodes[t][node].threshold;
            if (features[feat] <= thr)
                node = tree_nodes[t][node].left;
            else
                node = tree_nodes[t][node].right;
            if (tree_nodes[t][node].leaf) {
                votes[tree_nodes[t][node].cls]++;
                break;
            }
        }
    }
    // 返回得票最多的类别
    int best = 0;
    for (int c = 1; c < nb_classes; c++)
        if (votes[c] > votes[best]) best = c;
    return best;
}
```

**优势**：
- 不需要 TensorFlow / TFLite Micro / ESP-NN
- 不需要 PSRAM，RAM 开销 < 2KB
- 训练在 PC 上 10 秒完成
- 部署只需复制 C 数组 + 50 行推理代码

**局限**：
- 特征手动设计（均值+标准差），可能不如 CNN 自动特征提取准
- 但 3 类活动识别 + 1 秒窗口，足够达到 90%+ 准确率

---

## 9. 附录：工具链安装

### TFLite 路线

```bash
# onnx2tf 路线
pip install onnx onnxruntime onnx2tf

# ai_edge_torch 路线（推荐）
pip install ai-edge-torch

# 数据采集
pip install pyserial
```

### 最小 Demo 路线（无需额外安装）

```bash
# PyTorch + scikit-learn 已安装，只需 pyserial
pip install pyserial
```

### 参考项目

| 项目                         | 链接                                                               | 参考价值                     |
| -------------------------- | ---------------------------------------------------------------- | ------------------------ |
| TFLiteMicro_ArduinoESP32S3 | https://github.com/j-siderius/TFLiteMicro_ArduinoESP32S3         | TFLite Micro + ESP-NN 集成 |
| esp32pico_watch            | https://github.com/dht3218/esp32pico_watch                       | MFCC+CNN 端侧部署            |
| TensorFlow HAR 教程          | https://www.tensorflow.org/tutorials/structured_data/time_series | 时序分类入门                   |
