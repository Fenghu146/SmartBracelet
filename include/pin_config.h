#pragma once

#define XPOWERS_CHIP_AXP2101

#define LCD_DC 4
#define LCD_CS 5
#define LCD_SCK 6
#define LCD_MOSI 7
#define LCD_RST 38
#define LCD_BL 40
#define LCD_WIDTH 240
#define LCD_HEIGHT 284

#define IIC_SDA 15
#define IIC_SCL 14

#define TP_SDA IIC_SDA
#define TP_SCL IIC_SCL
#define TP_RST 39
#define TP_INT 13

// TF Card (SDMMC 1-bit mode)
#define SDMMC_CLK 2
#define SDMMC_CMD 1
#define SDMMC_D0  3

// ES8311 Audio Codec (I2S)
#define I2S_MCK  16
#define I2S_BCK  9
#define I2S_WS   45
#define I2S_DO   8
#define I2S_DI   10
#define PA_EN    46
#define ES8311_ADDR 0x18