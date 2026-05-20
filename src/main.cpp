#include <Arduino.h>
#include "pin_config.h"
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32SPI.h>
#include <display/Arduino_ST7789.h>
#include <lvgl.h>
#include "lv_port_disp.h"
#include <CST816S.h>
#include "lv_port_indev.h"
#include <Wire.h>
#include "SensorPCF85063.hpp"
#include "SensorQMI8658.hpp"

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;
SensorPCF85063 rtc;
SensorQMI8658 imu;

static void lvgl_ui_init(void) {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "SmartBracelet\nLVGL Ready!");
  lv_obj_set_style_text_color(label, lv_color_hex(0x00d4ff), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_center(label);
}

void setup() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  USBSerial.begin(115200);
  unsigned long start = millis();
  while (!USBSerial && millis() - start < 3000) {
    delay(10);
  }
  USBSerial.println("Booting...");

  Wire.begin(IIC_SDA, IIC_SCL);

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

  if (!gfx->begin()) {
    USBSerial.println("gfx FAIL");
    while (true) delay(100);
  }
  USBSerial.println("gfx OK");

  lv_init();
  lv_port_disp_init();
  USBSerial.println("LVGL disp OK");

  lvgl_ui_init();
  USBSerial.println("LVGL UI OK");

  touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);
  touch->begin();
  USBSerial.println("Touch init OK");

  lv_port_indev_init();

  if (rtc.init(Wire, IIC_SDA, IIC_SCL, PCF85063_SLAVE_ADDRESS)) {
    USBSerial.println("RTC init OK");
    RTC_DateTime dt = rtc.getDateTime();
    if (dt.year < 2026) {
      rtc.setDateTime(2026, 5, 20, 19, 0, 0);
      USBSerial.println("RTC time set");
    }
  } else {
    USBSerial.println("RTC init FAIL");
  }

  if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    USBSerial.println("IMU init OK");
    imu.configAccelerometer(
      SensorQMI8658::ACC_RANGE_4G,
      SensorQMI8658::ACC_ODR_1000Hz,
      SensorQMI8658::LPF_MODE_0, true);
    imu.configGyroscope(
      SensorQMI8658::GYR_RANGE_64DPS,
      SensorQMI8658::GYR_ODR_896_8Hz,
      SensorQMI8658::LPF_MODE_3, true);
    imu.enableGyroscope();
    imu.enableAccelerometer();
    USBSerial.println("IMU configured");
  } else {
    USBSerial.println("IMU init FAIL");
  }

  USBSerial.println("All done");
}

IMUdata acc, gyr;

void loop() {
  lv_timer_handler();
  delay(5);

  if (touch && touch->available()) {
    if (touch->data.x > 0 || touch->data.y > 0) {
      USBSerial.print("T ");
      USBSerial.print(touch->data.x);
      USBSerial.print(" ");
      USBSerial.print(touch->data.y);
      USBSerial.print(" ev=");
      USBSerial.print(touch->data.event);
      USBSerial.print(" gest=");
      USBSerial.println(touch->gesture());
    }
  }

  static unsigned long last = 0;
  if (millis() - last > 1000) {
    last = millis();
    RTC_DateTime dt = rtc.getDateTime();
    USBSerial.printf("%04d-%02d-%02d %02d:%02d:%02d",
      dt.year, dt.month, dt.day,
      dt.hour, dt.minute, dt.second);
    if (imu.getDataReady()) {
      if (imu.getAccelerometer(acc.x, acc.y, acc.z)) {
        USBSerial.printf(" A%.2f,%.2f,%.2f", acc.x, acc.y, acc.z);
      }
      if (imu.getGyroscope(gyr.x, gyr.y, gyr.z)) {
        USBSerial.printf(" G%.2f,%.2f,%.2f", gyr.x, gyr.y, gyr.z);
      }
    }
    USBSerial.println();
  }
}
