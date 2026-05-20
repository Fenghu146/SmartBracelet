#include <Arduino.h>
#include "pin_config.h"
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32SPI.h>
#include <display/Arduino_ST7789.h>
#include <lvgl.h>
#include "lv_port_disp.h"
#include <CST816S.h>
#include "lv_port_indev.h"

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;

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
  delay(500);
  USBSerial.println("Booting LVGL...");

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() FAILED");
    while (true) delay(100);
  }
  USBSerial.println("gfx OK");

  lv_init();
  lv_port_disp_init();
  USBSerial.println("LVGL display port OK");

  lvgl_ui_init();
  USBSerial.println("LVGL UI created");

  touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);
  touch->begin();
  USBSerial.println("Touch init OK");

  lv_port_indev_init();
  USBSerial.println("LVGL indev OK");
}

void loop() {
  lv_timer_handler();
  delay(5);
  static unsigned long last = 0;
  if (millis() - last > 2000) {
    last = millis();
    USBSerial.println("tick");
  }
}
