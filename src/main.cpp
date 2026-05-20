#include <Arduino.h>
#include "pin_config.h"
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32SPI.h>
#include <display/Arduino_ST7789.h>

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

void setup() {
  USBSerial.begin(115200);
  delay(500);
  USBSerial.println("Booting...");

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
  USBSerial.println("bus created");

  gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);
  USBSerial.println("gfx created");

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() FAILED");
    while (true);
  }
  USBSerial.println("gfx->begin() OK");

  gfx->fillScreen(RED);
  USBSerial.println("RED");
  delay(1000);

  gfx->fillScreen(GREEN);
  USBSerial.println("GREEN");
  delay(1000);

  gfx->fillScreen(BLUE);
  USBSerial.println("BLUE");
  delay(1000);

  gfx->fillScreen(BLACK);
  gfx->setCursor(10, 60);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);
  gfx->println("SmartBracelet");
  USBSerial.println("DONE");
}

void loop() {
  delay(1000);
}
