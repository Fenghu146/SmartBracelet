#include <Arduino.h>
#include "pin_config.h"
#include <Wire.h>
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32SPI.h>
#include <display/Arduino_ST7789.h>
#include <lvgl.h>
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "Arduino_DriveBus_Library.h"

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = nullptr;
std::unique_ptr<Arduino_IIC> touch = nullptr;

static void lvgl_ui_init(void) {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "SmartBracelet\nLVGL Ready!");
  lv_obj_set_style_text_color(label, lv_color_hex(0x00d4ff), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_center(label);
}

static void touch_interrupt(void) {
  if (touch) touch->IIC_Interrupt_Flag = true;
}

void setup() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  USBSerial.begin(115200);
  delay(500);
  USBSerial.println("Booting...");

  Wire.begin(IIC_SDA, IIC_SCL);

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

  if (!gfx->begin()) {
    USBSerial.println("gfx FAIL");
    while (true) delay(100);
  }
  USBSerial.println("gfx OK");

  IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
  touch = std::unique_ptr<Arduino_IIC>(new Arduino_CST816x(IIC_Bus, CST816T_DEVICE_ADDRESS, TP_RST, TP_INT, touch_interrupt));
  if (!touch->begin()) {
    USBSerial.println("Touch init FAIL");
  } else {
    USBSerial.println("Touch init OK");
    touch->IIC_Write_Device_State(
      Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
      Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
  }

  lv_init();
  lv_port_disp_init();
  lvgl_ui_init();
  lv_port_indev_init();
  USBSerial.println("LVGL ready");
}

void loop() {
  lv_timer_handler();
  delay(5);

  if (touch && touch->IIC_Interrupt_Flag == true) {
    touch->IIC_Interrupt_Flag = false;
    int32_t tx = touch->IIC_Read_Device_Value(
      Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
    int32_t ty = touch->IIC_Read_Device_Value(
      Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
    USBSerial.print("T ");
    USBSerial.print(tx);
    USBSerial.print(" ");
    USBSerial.println(ty);
  }

  static unsigned long last = 0;
  if (millis() - last > 2000) {
    last = millis();
    USBSerial.println("tick");
  }
}
