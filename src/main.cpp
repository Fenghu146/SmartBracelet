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
#include "service/wifi_ntp.h"
#include "service/ble_srv.h"
#include <math.h>

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;
SensorPCF85063 rtc;
SensorQMI8658 imu;
XPowersPMU pmu;

// UI
static lv_obj_t *time_label = nullptr;
static lv_obj_t *date_label = nullptr;
static lv_obj_t *battery_label = nullptr;
static lv_obj_t *step_label = nullptr;
static lv_obj_t *accel_label = nullptr;
static lv_obj_t *gyro_label = nullptr;
static lv_obj_t *wifi_icon = nullptr;
static lv_obj_t *ble_icon = nullptr;
static lv_obj_t *page_dots = nullptr;

IMUdata acc, gyr;
static int last_batt = -1;
static char time_str[12], date_str[32];
static int current_page = 0;
static const int NUM_PAGES = 3;
static lv_obj_t *pages[3];

// Step counter with improved algorithm
static int step_count = 0;
static float step_filt_mag = 1.0f;
static float step_baseline = 1.0f;
static int step_baseline_samples = 0;
static unsigned long last_step_time = 0;
static bool step_in_peak = false;
static const float STEP_LOCK_MS = 250;
static const float STEP_TIMEOUT_MS = 2000;

// Analog watchface objects
static lv_obj_t *analog_face = nullptr;
static lv_obj_t *hour_hand = nullptr;
static lv_obj_t *min_hand = nullptr;
static lv_obj_t *sec_hand = nullptr;
static lv_point_t hour_pts[2], min_pts[2], sec_pts[2];
static lv_point_t dial_pts[12][2];
static int prev_hour = -1, prev_min = -1, prev_sec = -1;
static bool analog_inited = false;

static const int CX = 120, CY = 142; // screen center
static lv_obj_t *dial_marks[12];

// NTP sync
static unsigned long last_ntp_attempt = 0;
static bool ntp_synced = false;

void set_rtc_from_tm(struct tm *ti) {
  rtc.setDateTime(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
    ti->tm_hour, ti->tm_min, ti->tm_sec);
  USBSerial.println("RTC: time updated from NTP");
}

static void set_dot(int active) {
  if (!page_dots) return;
  char dots[] = "○ ○ ○";
  dots[active * 2] = '●';
  lv_label_set_text(page_dots, dots);
}

static void status_bar_create(lv_obj_t *parent) {
  wifi_icon = lv_label_create(parent);
  lv_label_set_text(wifi_icon, "~");
  lv_obj_align(wifi_icon, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x555566), 0);

  ble_icon = lv_label_create(parent);
  lv_label_set_text(ble_icon, ")");
  lv_obj_align(ble_icon, LV_ALIGN_TOP_LEFT, 28, 6);
  lv_obj_set_style_text_font(ble_icon, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(ble_icon, lv_color_hex(0x555566), 0);

  battery_label = lv_label_create(parent);
  lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888899), 0);
  lv_label_set_text(battery_label, "BAT --");

  page_dots = lv_label_create(parent);
  lv_label_set_text(page_dots, "● ○ ○");
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

static void analog_create_hand(lv_obj_t **hand, lv_point_t pts[2],
    int len, int width, lv_color_t color) {
  *hand = lv_line_create(analog_face);
  pts[0].x = CX; pts[0].y = CY;
  pts[1].x = CX + len; pts[1].y = CY - len; // angle 0 = 12 o'clock
  lv_line_set_points(*hand, pts, 2);
  lv_obj_set_style_line_width(*hand, width, 0);
  lv_obj_set_style_line_color(*hand, color, 0);
  lv_obj_set_style_line_rounded(*hand, 1, 0);
}

static void analog_watchface_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);
  analog_face = parent;

  // Dial marks at 12 hour positions
  for (int i = 0; i < 12; i++) {
    float a = (float)i * M_PI / 6.0f - M_PI / 2.0f;
    int outer = 95, inner = 82;
    dial_pts[i][0].x = CX + (int)(cosf(a) * inner);
    dial_pts[i][0].y = CY + (int)(sinf(a) * inner);
    dial_pts[i][1].x = CX + (int)(cosf(a) * outer);
    dial_pts[i][1].y = CY + (int)(sinf(a) * outer);
    dial_marks[i] = lv_line_create(parent);
    lv_line_set_points(dial_marks[i], dial_pts[i], 2);
    lv_obj_set_style_line_width(dial_marks[i], i % 3 == 0 ? 3 : 1, 0);
    lv_obj_set_style_line_color(dial_marks[i], lv_color_hex(0x555566), 0);
  }

  analog_create_hand(&hour_hand, hour_pts, 50, 4, lv_color_hex(0xffffff));
  analog_create_hand(&min_hand, min_pts, 72, 3, lv_color_hex(0xcccccc));
  analog_create_hand(&sec_hand, sec_pts, 80, 1, lv_color_hex(0xff4444));
  analog_inited = true;
}

static void update_analog_hand(lv_obj_t *hand, lv_point_t pts[2],
    float angle_deg, int len) {
  float rad = angle_deg * M_PI / 180.0f;
  pts[1].x = CX + (int)(sinf(rad) * len);
  pts[1].y = CY - (int)(cosf(rad) * len);
  lv_line_set_points(hand, pts, 2);
}

static void update_analog_watchface(void) {
  if (!analog_inited) return;
  RTC_DateTime dt = rtc.getDateTime();
  int h = dt.hour % 12, m = dt.minute, s = dt.second;

  if (h == prev_hour && m == prev_min && s == prev_sec) return;
  prev_hour = h; prev_min = m; prev_sec = s;

  update_analog_hand(hour_hand, hour_pts, h * 30 + m * 0.5f, 50);
  update_analog_hand(min_hand, min_pts, m * 6, 72);
  update_analog_hand(sec_hand, sec_pts, s * 6, 80);
}

static void init_pages(void) {
  pages[0] = lv_obj_create(NULL); watchface_create(pages[0]);
  pages[1] = lv_obj_create(NULL); analog_watchface_create(pages[1]);
  pages[2] = lv_obj_create(NULL); sensor_page_create(pages[2]);
  lv_scr_load(pages[0]);
}

static void update_sensor_page(void) {
  lv_label_set_text_fmt(accel_label, "ACC %+03d %+03d %+03d",
    (int)(acc.x * 100), (int)(acc.y * 100), (int)(acc.z * 100));
  lv_label_set_text_fmt(gyro_label, "GYR %+04d %+04d %+04d",
    (int)(gyr.x * 10), (int)(gyr.y * 10), (int)(gyr.z * 10));
}

static void switch_page(int dir) {
  int next = current_page + dir;
  if (next < 0 || next >= NUM_PAGES) return;
  current_page = next;
  lv_scr_load_anim(pages[next],
    dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
    200, 0, false);
  set_dot(next);
}

static void update_watchface(void) {
  RTC_DateTime dt = rtc.getDateTime();
  snprintf(time_str, sizeof(time_str), "%02d:%02d", dt.hour, dt.minute);
  lv_label_set_text(time_label, time_str);

  static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(date_str, sizeof(date_str), "%s %s %d",
    wd[dt.week < 7 ? dt.week : 0],
    mo[dt.month >= 1 && dt.month <= 12 ? dt.month - 1 : 0], dt.day);
  lv_label_set_text(date_label, date_str);

  int batt = pmu.getBatteryPercent();
  if (batt >= 0 && batt != last_batt) {
    last_batt = batt;
    lv_label_set_text_fmt(battery_label, "BAT %d%%", batt);
    lv_obj_set_style_text_color(battery_label,
      batt < 20 ? lv_color_hex(0xff3333) : lv_color_hex(0x888899), 0);
    ble_srv_update_battery(batt);
  }

  lv_label_set_text_fmt(step_label, "Steps: %d", step_count);

  if (wifi_icon) {
    lv_obj_set_style_text_color(wifi_icon,
      wifi_is_connected() ? lv_color_hex(0x00d4ff) : lv_color_hex(0x333344), 0);
  }
}

static void handle_gesture(void) {
  int g = touch->data.gestureID;
  if (g == SWIPE_LEFT) switch_page(1);
  else if (g == SWIPE_RIGHT) switch_page(-1);
}

// Improved step counting: low-pass filter + adaptive baseline + time validation
static void update_step_count(void) {
  float raw = sqrtf(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
  step_filt_mag = 0.85f * step_filt_mag + 0.15f * raw;

  // Adaptive baseline: running average
  if (step_baseline_samples < 50) {
    step_baseline = 0.98f * step_baseline + 0.02f * step_filt_mag;
    step_baseline_samples++;
  } else if (fabs(step_filt_mag - step_baseline) < 0.05f) {
    step_baseline = 0.99f * step_baseline + 0.01f * step_filt_mag;
  }

  float threshold = step_baseline + 0.35f;
  unsigned long now = millis();

  if (!step_in_peak && step_filt_mag > threshold &&
      now - last_step_time > STEP_LOCK_MS) {
    step_count++;
    step_in_peak = true;
    last_step_time = now;
  }

  if (step_in_peak && step_filt_mag < step_baseline + 0.05f) {
    step_in_peak = false;
  }

  // Reset if no step for too long
  if (now - last_step_time > STEP_TIMEOUT_MS) {
    step_in_peak = false;
  }
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

  touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);
  touch->begin();
  lv_port_indev_init();

  if (rtc.init(Wire, IIC_SDA, IIC_SCL, PCF85063_SLAVE_ADDRESS)) {
    RTC_DateTime dt = rtc.getDateTime();
    if (dt.year < 2026) rtc.setDateTime(2026, 5, 20, 19, 0, 0);
  }

  if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
      SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0, true);
    imu.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS,
      SensorQMI8658::GYR_ODR_896_8Hz, SensorQMI8658::LPF_MODE_3, true);
    imu.enableGyroscope();
    imu.enableAccelerometer();
  }

  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
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
  }

  ble_srv_init();
  wifi_ntp_init();
  USBSerial.println("Ready");
}

void loop() {
  lv_timer_handler();
  wifi_ntp_loop();

  if (wifi_is_connected() && !ntp_synced) ntp_synced = wifi_ntp_sync();
  if (ntp_synced && millis() - last_ntp_attempt > 3600000) {
    last_ntp_attempt = millis(); wifi_ntp_sync();
  }

  if (ble_notification.has_new) {
    ble_notification.has_new = 0;
    USBSerial.printf("Notify: [%s] %s - %s\n",
      ble_notification.app_id, ble_notification.title, ble_notification.body);
    // Echo back to phone as ack
    char reply[64];
    snprintf(reply, sizeof(reply), "ack:%s", ble_notification.app_id);
    ble_srv_send(reply);
  }

  // Serial-to-BLE: any input sends immediately
  while (USBSerial.available()) {
    char c = USBSerial.read();
    char one[2] = {c, 0};
    ble_srv_send(one);
  }

  if (imu.getDataReady()) {
    imu.getAccelerometer(acc.x, acc.y, acc.z);
    imu.getGyroscope(gyr.x, gyr.y, gyr.z);
    update_step_count();
  }

  static unsigned long last_tick = 0;
  if (millis() - last_tick > 1000) {
    last_tick = millis();
    update_watchface();
    if (current_page == 1) update_analog_watchface();
    if (current_page == 2) update_sensor_page();
  }

  if (touch && touch->available()) {
    if (touch->data.x > 0 || touch->data.y > 0) handle_gesture();
  }

  delay(5);
}
