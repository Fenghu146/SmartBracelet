# SmartBracelet — ESP32-S3 智能手表

## 硬件

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-S3, 240MHz, 16MB Flash, 8MB OPI PSRAM |
| 屏幕 | ST7789, 240×284, SPI (DC=4, CS=5, SCK=6, MOSI=7, RST=38, BL=40) |
| 触控 | CST816S (SDA=19, SCL=20, RST=39, INT=13) — **待确认引脚** |
| PMU | AXP2101 |
| IMU | QMI8658 (I2C: SDA=15, SCL=14) |
| 连接 | USB CDC (COM9), VID:PID=303A:1001 |

## 当前状态

- ✅ Arduino_GFX + LVGL 8.4.0 显示正常
- ✅ USBSerial 串口输出正常（带 3 秒超时）
- ✅ LVGL 标签文字渲染正常
- ❌ 触控未启用（引脚可能冲突）
- ❌ LVGL 小部件未开始构建 UI

## 关键配置

`board = esp32-s3-devkitc-1`，PSRAM 和 USB 配置通过 build_flags 添加（**不能**用自定义板子 `ESP32-S3-R8-OPI`，会导致 USBSerial 未定义）。上传速度用 115200，flash 用 QIO 模式（eFuse 锁定）。

## 已知问题 / 坑

1. **USB 带电插拔损毁 flash** — 操作 OTA 或配置保存时不要拔 USB；救活方法：erase_flash → 重烧
2. **RTS 复位不可靠** — 上传后需手动拔插 USB 冷启动
3. **板子卡下载模式** — 松开 BOOT 后需要拔插 USB（不是按 RST）才能正常启动
4. **TP_SDA/SCL=19/20 与原生 USB 冲突** — 触控 I2C 与 USBSerial 共用 GPIO19/20，硬件设计待确认

## 交接指令

当我说"交接"时，你需要：
1. 检查 git status，了解当前工作状态
2. 更新 DEBUG_REPORT.md，追加最近的调试/开发记录
3. 如果有代码改动且未提交，执行 git add + git commit
4. 输出当前项目状态摘要（什么在做、什么卡住、下一步建议）

## 调试报告

详细的故障排查记录在 `DEBUG_REPORT.md`，包含三次调试：
- 白屏/启动循环修复
- USB 插拔后 flash 损坏恢复
- LVGL 集成记录
