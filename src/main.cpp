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
#include "stopwatch.h"
#include "weather.h"
#include "activity.h"
#include "player.h"
#include "service/tf_card.h"
#include "service/audio.h"
#include "service/voice_chat.h"
#include "voice_chat_ui.h"
#include <math.h>
#include <esp_sleep.h>

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
static lv_obj_t *battery_bar = nullptr;
static lv_obj_t *charging_label = nullptr;
static lv_obj_t *step_label = nullptr;
static lv_obj_t *accel_label = nullptr;
static lv_obj_t *gyro_label = nullptr;
static lv_obj_t *batt_volt_label = nullptr;
static lv_obj_t *wifi_icon = nullptr;
static lv_obj_t *ble_icon = nullptr;
static lv_obj_t *page_dots = nullptr;

IMUdata acc, gyr;
static int last_batt = -1;
static char time_str[12], date_str[32];
static int current_page = 0;
static const int NUM_PAGES = 9;
static lv_obj_t *pages[9];

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

// Activity state for BLE data service
static int current_activity = -1; // -1=unknown, 0=walk, 1=run, 2=idle

// NTP sync
static unsigned long last_ntp_attempt = 0;
static bool ntp_synced = false;

// Screen timeout & wrist raise
static unsigned long last_activity_time = 0;
static bool screen_on = true;
static const unsigned long DISPLAY_TIMEOUT_MS = 10000;
static const unsigned long DEEP_SLEEP_TIMEOUT_MS = 30000;

static float grav_x = 0, grav_y = 0, grav_z = 0;
static float rest_grav_x = 0, rest_grav_y = 0, rest_grav_z = 0;
static bool rest_ready = false;
static int rest_count = 0;
static unsigned long wrist_raise_time = 0;
static bool wrist_is_raised = false;

// Notification page state
static lv_obj_t *notif_title = nullptr;
static lv_obj_t *notif_body = nullptr;
static lv_obj_t *notif_app = nullptr;

void set_rtc_from_tm(struct tm *ti) {
  rtc.setDateTime(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
    ti->tm_hour, ti->tm_min, ti->tm_sec);
  USBSerial.println("RTC: time updated from NTP");
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

  // Battery icon bar (right side)
  battery_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(battery_bar);
  lv_obj_set_size(battery_bar, 24, 10);
  lv_obj_set_style_border_width(battery_bar, 1, 0);
  lv_obj_set_style_border_color(battery_bar, lv_color_hex(0x888899), 0);
  lv_obj_set_style_radius(battery_bar, 1, 0);
  lv_obj_set_style_pad_all(battery_bar, 1, 0);
  lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x111122), 0);
  lv_obj_set_style_bg_opa(battery_bar, LV_OPA_COVER, 0);
  lv_obj_align(battery_bar, LV_ALIGN_TOP_RIGHT, -28, 7);

  // Battery terminal (tiny rectangle on the right)
  lv_obj_t *bt = lv_obj_create(battery_bar);
  lv_obj_remove_style_all(bt);
  lv_obj_set_size(bt, 2, 4);
  lv_obj_set_style_bg_color(bt, lv_color_hex(0x888899), 0);
  lv_obj_set_style_bg_opa(bt, LV_OPA_COVER, 0);
  lv_obj_align(bt, LV_ALIGN_RIGHT_MID, 2, 0);
  lv_obj_set_style_radius(bt, 0, 0);

  // Battery fill bar (inside the battery icon)
  lv_obj_t *bf = lv_obj_create(battery_bar);
  lv_obj_remove_style_all(bf);
  lv_obj_set_size(bf, 20, 6);
  lv_obj_set_style_bg_color(bf, lv_color_hex(0x00d4ff), 0);
  lv_obj_set_style_bg_opa(bf, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bf, 0, 0);
  lv_obj_align(bf, LV_ALIGN_LEFT_MID, 1, 0);
  battery_bar = bf;

  charging_label = lv_label_create(parent);
  lv_label_set_text(charging_label, "");
  lv_obj_align(charging_label, LV_ALIGN_TOP_RIGHT, -56, 5);
  lv_obj_set_style_text_font(charging_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(charging_label, lv_color_hex(0x00d488), 0);

  battery_label = lv_label_create(parent);
  lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888899), 0);
  lv_label_set_text(battery_label, "--");

  page_dots = lv_label_create(parent);
  lv_label_set_text(page_dots, "● ○ ○ ○ ○ ○ ○ ○");
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

  batt_volt_label = lv_label_create(parent);
  lv_label_set_text(batt_volt_label, "BAT: --");
  lv_obj_set_style_text_font(batt_volt_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(batt_volt_label, lv_color_hex(0x00d488), 0);
  lv_obj_align(batt_volt_label, LV_ALIGN_LEFT_MID, 10, 40);
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

static void notif_page_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  notif_app = lv_label_create(parent);
  lv_label_set_text(notif_app, "No notifications");
  lv_obj_set_style_text_font(notif_app, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(notif_app, lv_color_hex(0x555566), 0);
  lv_obj_align(notif_app, LV_ALIGN_TOP_LEFT, 8, 10);

  notif_title = lv_label_create(parent);
  lv_label_set_text(notif_title, "");
  lv_obj_set_style_text_font(notif_title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(notif_title, lv_color_hex(0xffffff), 0);
  lv_obj_align(notif_title, LV_ALIGN_TOP_LEFT, 8, 28);

  notif_body = lv_label_create(parent);
  lv_label_set_text(notif_body, "");
  lv_obj_set_style_text_font(notif_body, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(notif_body, lv_color_hex(0xcccccc), 0);
  lv_obj_align(notif_body, LV_ALIGN_TOP_LEFT, 8, 50);
  lv_label_set_long_mode(notif_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(notif_body, LCD_WIDTH - 16);
}

static void update_notif_page(void) {
  if (strlen(ble_notification.app_id) > 0) {
    lv_label_set_text(notif_app, ble_notification.app_id);
    lv_label_set_text(notif_title, ble_notification.title);
    lv_label_set_text(notif_body, ble_notification.body);
  }
}

static void init_pages(void) {
  pages[0] = lv_obj_create(NULL); watchface_create(pages[0]);
  pages[1] = lv_obj_create(NULL); analog_watchface_create(pages[1]);
  pages[2] = lv_obj_create(NULL); sensor_page_create(pages[2]);
  pages[3] = lv_obj_create(NULL); notif_page_create(pages[3]);
  pages[4] = lv_obj_create(NULL); stopwatch_create(pages[4]);
  pages[5] = lv_obj_create(NULL); weather_create(pages[5]);
  pages[6] = lv_obj_create(NULL); activity_create(pages[6]);
  pages[7] = lv_obj_create(NULL); player_create(pages[7]);
  pages[8] = lv_obj_create(NULL); voice_chat_page_create(pages[8]);
  status_bar_create(lv_layer_top());
  lv_scr_load(pages[0]);
}

static uint16_t read_batt_voltage_raw(void) {
  int h5 = pmu.readRegister(0x34);
  int l8 = pmu.readRegister(0x35);
  if (h5 < 0 || l8 < 0) return 0;
  return ((h5 & 0x1F) << 8) | l8;
}
static int read_batt_percent_raw(void) {
  return pmu.readRegister(0xA4);
}
static bool batt_is_valid(void) {
  uint16_t mv = read_batt_voltage_raw();
  return (mv >= 500 && mv <= 5000);  // reject floating ADC noise
}

static void update_sensor_page(void) {
  lv_label_set_text_fmt(accel_label, "ACC %+03d %+03d %+03d",
    (int)(acc.x * 100), (int)(acc.y * 100), (int)(acc.z * 100));
  lv_label_set_text_fmt(gyro_label, "GYR %+04d %+04d %+04d",
    (int)(gyr.x * 10), (int)(gyr.y * 10), (int)(gyr.z * 10));
  if (batt_is_valid()) {
    lv_label_set_text_fmt(batt_volt_label, "BAT %dmV %d%%",
      read_batt_voltage_raw(), read_batt_percent_raw());
  } else {
    lv_label_set_text(batt_volt_label, "BAT USB");
  }
}

static void switch_page(int dir) {
  int next = current_page + dir;
  if (next < 0 || next >= NUM_PAGES) return;
  current_page = next;
  lv_scr_load_anim(pages[next],
    dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
    200, 0, false);
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

  int batt = read_batt_percent_raw();
  bool batt_ok = batt_is_valid();
  if (batt_ok && batt >= 0 && batt <= 100 && batt != last_batt) {
    last_batt = batt;
    lv_label_set_text_fmt(battery_label, "%d%%", batt);
    lv_obj_set_style_text_color(battery_label,
      batt < 20 ? lv_color_hex(0xff3333) : lv_color_hex(0x888899), 0);
    // Update battery bar width (max 20px)
    int bw = (batt * 20) / 100;
    if (bw < 2 && batt > 0) bw = 2;
    lv_obj_set_width(battery_bar, bw);
    lv_obj_set_style_bg_color(battery_bar,
      batt < 20 ? lv_color_hex(0xff3333) :
      batt < 50 ? lv_color_hex(0xffaa00) : lv_color_hex(0x00d4ff), 0);
    ble_srv_update_battery(batt);
  } else if (!batt_ok && last_batt != -2) {
    last_batt = -2;
    lv_label_set_text(battery_label, "USB");
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0x00d488), 0);
    lv_obj_set_width(battery_bar, 20);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x00d488), 0);
  }

  if (charging_label) {
    xpowers_chg_status_t cs = pmu.getChargerStatus();
    bool chg = (cs == XPOWERS_AXP2101_CHG_CC_STATE ||
                cs == XPOWERS_AXP2101_CHG_PRE_STATE ||
                cs == XPOWERS_AXP2101_CHG_TRI_STATE);
    lv_label_set_text(charging_label, chg ? "+" : "");
    lv_obj_set_style_text_color(charging_label,
      chg ? lv_color_hex(0x00d488) : lv_color_hex(0x111122), 0);
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

static void set_backlight(bool on) {
  digitalWrite(LCD_BL, on ? HIGH : LOW);
  screen_on = on;
}

static void reset_activity_timer(void) {
  last_activity_time = millis();
  if (!screen_on) set_backlight(true);
}

static void update_wrist_detect(void) {
  // Gravity vector (low-pass filter, ~0.5s time constant at 200Hz)
  float alpha = 0.02f;
  grav_x = (1 - alpha) * grav_x + alpha * acc.x;
  grav_y = (1 - alpha) * grav_y + alpha * acc.y;
  grav_z = (1 - alpha) * grav_z + alpha * acc.z;

  // Motion level: high-pass energy in raw acceleration
  static float prev_raw = 1.0f;
  float raw = sqrtf(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
  float jerk = fabsf(raw - prev_raw);
  prev_raw = raw;
  static float motion = 0;
  motion = 0.8f * motion + 0.2f * jerk;

  // Update rest gravity baseline when stationary
  if (motion < 0.03f) {
    rest_count++;
    if (rest_count > 100 && !rest_ready) {
      rest_grav_x = grav_x; rest_grav_y = grav_y; rest_grav_z = grav_z;
      rest_ready = true;
      rest_count = 101;
    } else if (rest_count > 300) {
      rest_grav_x = 0.98f * rest_grav_x + 0.02f * grav_x;
      rest_grav_y = 0.98f * rest_grav_y + 0.02f * grav_y;
      rest_grav_z = 0.98f * rest_grav_z + 0.02f * grav_z;
    }
  } else {
    rest_count = 0;
  }

  if (!rest_ready || wrist_is_raised) {
    if (wrist_is_raised && millis() - wrist_raise_time > 3000)
      wrist_is_raised = false;
    return;
  }

  // Gravity angle between current and rest
  float dot = grav_x * rest_grav_x + grav_y * rest_grav_y + grav_z * rest_grav_z;
  float g = sqrtf(grav_x * grav_x + grav_y * grav_y + grav_z * grav_z);
  float r = sqrtf(rest_grav_x * rest_grav_x + rest_grav_y * rest_grav_y + rest_grav_z * rest_grav_z);
  if (g < 0.01f || r < 0.01f) return;

  float cos_a = dot / (g * r);
  if (cos_a > 1.0f) cos_a = 1.0f;
  if (cos_a < -1.0f) cos_a = -1.0f;
  float angle = acosf(cos_a) * 57.2958f; // rad → deg

  // Wrist raise = motion + orientation change
  if (motion > 0.08f && angle > 25.0f) {
    wrist_is_raised = true;
    wrist_raise_time = millis();
    reset_activity_timer();
  }
}

void setup() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  last_activity_time = millis();

  USBSerial.begin(115200);
  unsigned long start = millis();
  while (!USBSerial && millis() - start < 3000) delay(10);
  USBSerial.println("Booting...");

  Wire.begin(IIC_SDA, IIC_SCL);

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);
  if (!gfx->begin()) { while (true) delay(100); }

  // Clear physical rows 0-19 (outside LVGL space due to _yStart=20)
  // and rows 304-319 at the bottom, since ST7789 is 320 rows tall.
  bus->beginWrite();
  bus->writeC8D16D16(ST7789_CASET, 0, 239);
  bus->writeC8D16D16(ST7789_RASET, 0, 19);
  bus->writeCommand(ST7789_RAMWR);
  bus->writeRepeat(0x0000, 240 * 20);
  bus->writeC8D16D16(ST7789_RASET, 304, 319);
  bus->writeCommand(ST7789_RAMWR);
  bus->writeRepeat(0x0000, 240 * 16);
  bus->endWrite();

  lv_init();
  lv_port_disp_init();
  tf_init();
  audio_init();
  audio_init_rx();
  voice_chat_init();
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
    pmu.enableBattDetection();
    pmu.enableGauge();
    pmu.fuelGaugeControl(false, true);
    // Charging config: 4.35V target, 500mA constant current
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V35);
    pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_200MA);
    pmu.disableTSPinMeasure();
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ); pmu.clearIrqStatus();
    // Force BATFET ON + try CHG_EN bit
    pmu.writeRegister(0x12, 0x01);    // enable BATFET over-temp detect
    int icc = pmu.readRegister(0x62);
    pmu.writeRegister(0x62, icc | 0x80); // try setting bit 7 (CHG_EN)
    delay(200);
    int raw_status1 = pmu.readRegister(0x00);
    int raw_adc_ctrl = pmu.readRegister(0x30);
    int raw_batdet_ctrl = pmu.readRegister(0x68);
    int raw_batfet = pmu.readRegister(0x12);
    // Read raw ADC registers for battery voltage (0x34 high 5bits, 0x35 low 8bits)
    int raw_adc_h = pmu.readRegister(0x34);
    int raw_adc_l = pmu.readRegister(0x35);
    int raw_percent = pmu.readRegister(0xA4);
    // Also read VBUS and system voltage registers for comparison
    int raw_vbus_h = pmu.readRegister(0x38);
    int raw_vbus_l = pmu.readRegister(0x39);
    int raw_sys_h = pmu.readRegister(0x36);
    int raw_sys_l = pmu.readRegister(0x37);
    uint16_t raw_batt_mv = ((raw_adc_h & 0x1F) << 8) | raw_adc_l;
    uint16_t raw_vbus = ((raw_vbus_h & 0x3F) << 8) | raw_vbus_l;
    uint16_t raw_sys = ((raw_sys_h & 0x1F) << 8) | raw_sys_l;
    USBSerial.printf("PMU: STATUS1=0x%02x ADC_CTRL=0x%02x BATFET=0x%02x DET_CTRL=0x%02x\n",
      raw_status1, raw_adc_ctrl, raw_batfet, raw_batdet_ctrl);
    USBSerial.printf("PMU: batt_adc=%dmV vbus_adc=%dmV sys_adc=%dmV raw_percent=%d\n",
      raw_batt_mv, raw_vbus, raw_sys, raw_percent);
    USBSerial.printf("PMU: connected=%d vbus_in=%d vbus_good=%d chg=%d\n",
      pmu.isBatteryConnect(), pmu.isVbusIn(), pmu.isVbusGood(),
      pmu.getChargerStatus());
  }

  ble_srv_init();
  wifi_ntp_init();
  USBSerial.println("Ready");
}

void loop() {
  lv_timer_handler();
  voice_chat_loop();
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

  // Serial-to-BLE: buffer until Enter, support UTF-8 Chinese
  static char serial_buf[256];
  static int serial_len = 0;
  while (USBSerial.available() && serial_len < 255) {
    char c = USBSerial.read();
    if (c == '\n' || c == '\r') {
      if (serial_len > 0) {
        serial_buf[serial_len] = 0;
        ble_srv_send(serial_buf);
        serial_len = 0;
      }
    } else {
      serial_buf[serial_len++] = c;
    }
  }

  if (imu.getDataReady()) {
    imu.getAccelerometer(acc.x, acc.y, acc.z);
    imu.getGyroscope(gyr.x, gyr.y, gyr.z);
    activity_push_data(acc.x, acc.y, acc.z, gyr.x, gyr.y, gyr.z);
    update_step_count();
    update_wrist_detect();
  }

  static unsigned long last_tick = 0;
  if (millis() - last_tick > 1000) {
    last_tick = millis();
    update_watchface();
    if (current_page == 1) update_analog_watchface();
    if (current_page == 2) update_sensor_page();
    if (current_page == 3) update_notif_page();
    if (current_page == 5) weather_update();
    if (current_page == 6) activity_update();
    if (current_page == 7) player_update();
    if (current_page == 8) voice_chat_page_update();

    // Push telemetry to BLE data service
    int act = activity_get_current();
    if (act != current_activity) {
      current_activity = act;
      ble_srv_update_activity(act >= 0 ? (uint8_t)act : 2);
    }
    ble_srv_update_steps(step_count);
    if (batt_is_valid())
      ble_srv_update_batt_raw(read_batt_voltage_raw());
    else if (pmu.isVbusIn())
      ble_srv_update_batt_raw(0xFFFF);  // USB powered
  }

  // Stopwatch needs sub-second precision
  if (current_page == 4) stopwatch_update();

  // Screen timeout
  if (screen_on && millis() - last_activity_time > DISPLAY_TIMEOUT_MS) {
    set_backlight(false);
  }

  // Deep sleep timeout
  if (!screen_on && millis() - last_activity_time > DEEP_SLEEP_TIMEOUT_MS) {
    // Don't deep sleep when USB is plugged in (charging needs serial alive)
    if (pmu.isVbusIn()) {
      last_activity_time = millis(); // keep resetting, never sleep while on USB
    } else {
      USBSerial.println("Entering deep sleep...");
      delay(100);
      // Configure PMU for low power before sleep
      pmu.disableDC1();
      pmu.disableALDO1();
      pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
      // Wake every 60s (for periodic RTC check) or on touch
      esp_sleep_enable_timer_wakeup(60000000);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)TP_INT, 0);
      esp_deep_sleep_start();
    }
  }

  // Gesture handling: read from touch data (already populated by LVGL via touchpad_read)
  // Don't call touch->available() here — it would steal data from LVGL's input driver.
  if (touch) {
    static uint8_t last_gesture = 0xFF;
    static bool last_pressed = false;
    uint8_t g = touch->data.gestureID;
    bool pressed = (touch->data.event == 0) &&
                   (touch->data.x > 0 || touch->data.y > 0);

    // Any touch press resets timer and wakes screen
    if (pressed && !last_pressed) {
      reset_activity_timer();
    }
    last_pressed = pressed;

    // Handle swipe gestures (page navigation)
    if (g != last_gesture) {
      last_gesture = g;
      if (g == SWIPE_LEFT || g == SWIPE_RIGHT) {
        handle_gesture();
      }
    }
  }

  delay(5);
}
