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
#include "XPowersLib.h"

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;
SensorPCF85063 rtc;
SensorQMI8658 imu;
XPowersPMU pmu;

// UI objects
static lv_obj_t *time_label = nullptr;
static lv_obj_t *date_label = nullptr;
static lv_obj_t *battery_label = nullptr;
static lv_obj_t *step_label = nullptr;
static lv_obj_t *accel_label = nullptr;
static lv_obj_t *gyro_label = nullptr;

IMUdata acc, gyr;
static int last_batt = -1;
static char time_str[12], date_str[32], batt_str[8];
static int current_page = 0;
static const int NUM_PAGES = 2;

// Simple step counter
static int step_count = 0;
static float acc_mag_prev = 0;
static bool step_ready = true;
static const float STEP_THRESHOLD = 0.4f;

// Screens
static lv_obj_t *pages[2];

static void status_bar_create(lv_obj_t *parent) {
  battery_label = lv_label_create(parent);
  lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888899), 0);
  lv_label_set_text(battery_label, "BAT --");

  lv_obj_t *page_dots = lv_label_create(parent);
  lv_label_set_text(page_dots, "●   ○");
  lv_obj_align(page_dots, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_style_text_font(page_dots, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(page_dots, lv_color_hex(0x555566), 0);
}

static void watchface_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  time_label = lv_label_create(parent);
  lv_label_set_text(time_label, "00:00");
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
  lv_obj_center(time_label);
  lv_obj_set_y(time_label, lv_obj_get_y(time_label) - 24);

  date_label = lv_label_create(parent);
  lv_label_set_text(date_label, "---");
  lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(date_label, lv_color_hex(0x888899), 0);
  lv_obj_align_to(date_label, time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  step_label = lv_label_create(parent);
  lv_label_set_text(step_label, "Steps: 0");
  lv_obj_set_style_text_font(step_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(step_label, lv_color_hex(0x00d4ff), 0);
  lv_obj_align(step_label, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void sensor_page_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Sensors");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  accel_label = lv_label_create(parent);
  lv_label_set_text(accel_label, "ACC: --");
  lv_obj_set_style_text_font(accel_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(accel_label, lv_color_hex(0xcccccc), 0);
  lv_obj_align(accel_label, LV_ALIGN_LEFT_MID, 10, -20);

  gyro_label = lv_label_create(parent);
  lv_label_set_text(gyro_label, "GYR: --");
  lv_obj_set_style_text_font(gyro_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(gyro_label, lv_color_hex(0xcccccc), 0);
  lv_obj_align(gyro_label, LV_ALIGN_LEFT_MID, 10, 10);
}

static void init_pages(void) {
  pages[0] = lv_obj_create(NULL);
  watchface_create(pages[0]);
  pages[1] = lv_obj_create(NULL);
  sensor_page_create(pages[1]);
  lv_scr_load(pages[0]);
}

static void update_sensor_page(void) {
  if (accel_label) {
    lv_label_set_text_fmt(accel_label, "ACC %+03d %+03d %+03d",
      (int)(acc.x * 100), (int)(acc.y * 100), (int)(acc.z * 100));
  }
  if (gyro_label) {
    lv_label_set_text_fmt(gyro_label, "GYR %+04d %+04d %+04d",
      (int)(gyr.x * 10), (int)(gyr.y * 10), (int)(gyr.z * 10));
  }
}

static void switch_page(int dir) {
  int next = current_page + dir;
  if (next < 0 || next >= NUM_PAGES) return;
  current_page = next;
  lv_scr_load_anim(next > current_page - dir ? pages[next] : pages[next],
    dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
    200, 0, false);
  // Update page dots
}

static void update_watchface(void) {
  RTC_DateTime dt = rtc.getDateTime();

  snprintf(time_str, sizeof(time_str), "%02d:%02d", dt.hour, dt.minute);
  lv_label_set_text(time_label, time_str);

  static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int idx = (dt.week < 7) ? dt.week : 0;
  int mi = (dt.month >= 1 && dt.month <= 12) ? dt.month - 1 : 0;
  snprintf(date_str, sizeof(date_str), "%s %s %d", wd[idx], mo[mi], dt.day);
  lv_label_set_text(date_label, date_str);

  int batt = pmu.getBatteryPercent();
  if (batt >= 0 && batt != last_batt) {
    last_batt = batt;
    lv_label_set_text_fmt(battery_label, "BAT %d%%", batt);
    lv_obj_set_style_text_color(battery_label,
      batt < 20 ? lv_color_hex(0xff3333) : lv_color_hex(0x888899), 0);
  }

  lv_label_set_text_fmt(step_label, "Steps: %d", step_count);
}

static void handle_gesture(void) {
  int g = touch->data.gestureID;
  if (g == SWIPE_LEFT) switch_page(1);
  else if (g == SWIPE_RIGHT) switch_page(-1);
}

static void update_step_count(void) {
  float mag = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
  if (step_ready && mag > 1.0f + STEP_THRESHOLD && acc_mag_prev <= 1.0f + STEP_THRESHOLD) {
    step_count++;
    step_ready = false;
  }
  if (mag < 1.0f + STEP_THRESHOLD * 0.5f) {
    step_ready = true;
  }
  acc_mag_prev = mag;
}

void setup() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  USBSerial.begin(115200);
  unsigned long start = millis();
  while (!USBSerial && millis() - start < 3000) delay(10);
  USBSerial.println("Booting...");

  Wire.begin(IIC_SDA, IIC_SCL);

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);
  if (!gfx->begin()) { while (true) delay(100); }

  lv_init();
  lv_port_disp_init();
  init_pages();
  USBSerial.println("UI OK");

  touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);
  touch->begin();
  lv_port_indev_init();
  USBSerial.println("Touch OK");

  if (rtc.init(Wire, IIC_SDA, IIC_SCL, PCF85063_SLAVE_ADDRESS)) {
    USBSerial.println("RTC OK");
    RTC_DateTime dt = rtc.getDateTime();
    if (dt.year < 2026) {
      rtc.setDateTime(2026, 5, 20, 19, 0, 0);
    }
  }

  if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    USBSerial.println("IMU OK");
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
      SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0, true);
    imu.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS,
      SensorQMI8658::GYR_ODR_896_8Hz, SensorQMI8658::LPF_MODE_3, true);
    imu.enableGyroscope();
    imu.enableAccelerometer();
  } else { USBSerial.println("IMU FAIL"); }

  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    USBSerial.println("PMU OK");
    pmu.disableDC2(); pmu.disableDC3(); pmu.disableDC4(); pmu.disableDC5();
    pmu.disableALDO2(); pmu.disableALDO3(); pmu.disableALDO4();
    pmu.disableBLDO1(); pmu.disableBLDO2();
    pmu.disableCPUSLDO(); pmu.disableDLDO1(); pmu.disableDLDO2();
    pmu.setDC1Voltage(3300); pmu.enableDC1();
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();
    pmu.enableVbusVoltageMeasure(); pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.disableTSPinMeasure();
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ); pmu.clearIrqStatus();
  } else { USBSerial.println("PMU FAIL"); }

  USBSerial.println("Ready");
}

void loop() {
  lv_timer_handler();

  // Read IMU at ~200Hz for step counting
  if (imu.getDataReady()) {
    imu.getAccelerometer(acc.x, acc.y, acc.z);
    imu.getGyroscope(gyr.x, gyr.y, gyr.z);
    update_step_count();
  }

  static unsigned long last_tick = 0;
  if (millis() - last_tick > 1000) {
    last_tick = millis();
    update_watchface();
    if (current_page == 1) update_sensor_page();
  }

  // Gesture handling
  if (touch && touch->available()) {
    if (touch->data.x > 0 || touch->data.y > 0) {
      handle_gesture();
    }
  }

  delay(5);
}
