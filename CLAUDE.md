# SmartBracelet — ESP32-S3 智能手表

## 硬件

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-S3, 240MHz, 16MB Flash, 8MB OPI PSRAM |
| 屏幕 | ST7789, 240×284, SPI (DC=4, CS=5, SCK=6, MOSI=7, RST=38, BL=40) |
| 触控 | CST816D via CST816S 库 (IIC_SDA=15, IIC_SCL=14, RST=39, INT=13) |
| PMU | AXP2101 (I2C 0x34) |
| IMU | QMI8658 (I2C 0x6A) |
| RTC | PCF85063 (I2C 0x51) |
| 连接 | USB CDC (COM9), VID:PID=303A:1001 |

## 当前状态 (2026-05-21)

- ✅ 显示: Arduino_GFX + LVGL 8.4.0, 偏移 (0,20), RGB565 SWAP=0
- ✅ 触控: CST816S 兼容驱动, IIC 15/14, 手势左/右滑动翻页
- ✅ RTC: PCF85063 时间读写
- ✅ IMU: QMI8658 加速度+陀螺仪, 计步算法
- ✅ PMU: AXP2101 电量读取, 电源轨配置
- ✅ UI: 数字表盘 + 模拟表盘 + 传感器页 + 通知页, 4页滑动导航
- ✅ WiFi NTP: 自动连接 + NTP 校时
- ✅ BLE: 通知推送 (nRF Connect -> 手表), 双向通讯
- ✅ 抬手亮屏: IMU 抬腕检测 + 背光自动管理 + 息屏定时 (10s)
- ✅ Deep sleep: AXP2101 低功耗 + 触摸/定时唤醒 (30s 超时)
- ❌ 手机 App

## 关键配置

`board = esp32-s3-devkitc-1`，`board_build.flash_mode = qio`（eFuse 锁定）。上传速度 115200，`--after hard_reset`。当前 RAM 148KB/320KB (45%), Flash 1.6MB/3.3MB (49%)。

## 已知问题 / 坑

1. **USB 带电插拔损毁 flash** — 操作 OTA 或配置保存时不要拔 USB；救活：erase_flash → 重烧
2. **RTS 复位不可靠** — 上传后需手动拔插 USB 冷启动
3. **板子卡下载模式** — 松开 BOOT 后需要拔插 USB 才能正常启动
4. **Boot Loop 修复** — IRAM 溢出导致 TG0WDT 复位，精简 lib_deps（移除 LovyanGFX/Arduino_DriveBus）后解决

## 交接指令

当我说"交接"时，你需要：
1. 检查 git status，了解当前工作状态
2. 更新 DEBUG_REPORT.md，追加最近的调试/开发记录
3. 如果有代码改动且未提交，执行 git add + git commit
4. 输出当前项目状态摘要（什么在做、什么卡住、下一步建议）

## 调试报告

详细的故障排查记录在 `DEBUG_REPORT.md`，包含 8 次调试：
- 白屏/启动循环修复
- USB 插拔后 flash 损坏恢复
- LVGL 集成
- LVGL 验证 + 触摸引脚修正
- Boot Loop 修复（Arduino_DriveBus 回退）
- 传感器集成 + 表盘 UI
- BLE 双向通信 + 中文支持
- 抬手亮屏 + 通知页面 + Deep sleep
