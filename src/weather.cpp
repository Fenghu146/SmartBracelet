#include "weather.h"
#include "nvs_store.h"
#include "service/wifi_ntp.h"
#include "debug_log.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#define WEATHER_REFRESH_MS 1800000 // 30 min cache
#define WEATHER_LAT "39.9042"
#define WEATHER_LON "116.4074"
#define WEATHER_CITY "Beijing"

static lv_obj_t *temp_label = nullptr;
static lv_obj_t *humid_label = nullptr;
static lv_obj_t *cond_label = nullptr;
static lv_obj_t *status_label = nullptr;

static float last_temp = 0;
static int last_humid = 0;
static int last_code = -1;
static unsigned long last_fetch = 0;
static bool has_data = false;

static const char *wmo_code_str(int code) {
  if (code == 0) return "Clear";
  if (code <= 3)  return "Cloudy";
  if (code <= 19) return "Foggy";
  if (code <= 29) return "Rain";
  if (code <= 39) return "Drizzle";
  if (code <= 49) return "Rainy";
  if (code <= 59) return "Drizzle";
  if (code <= 69) return "Rainy";
  if (code <= 79) return "Snow";
  if (code <= 89) return "Showers";
  if (code <= 99) return "Storm";
  return "---";
}

static void weather_fetch(void) {
  if (!wifi_is_connected()) return;

  WiFiClient client;
  HTTPClient http;

  char url[256];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=" WEATHER_LAT "&longitude=" WEATHER_LON
    "&current=temperature_2m,relative_humidity_2m,weather_code");

  http.begin(client, url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      last_temp = doc["current"]["temperature_2m"];
      last_humid = doc["current"]["relative_humidity_2m"];
      last_code = doc["current"]["weather_code"];
      has_data = true;
      last_fetch = millis();
      LOG_INFO("Weather: %.1fC %d%% code=%d (" WEATHER_CITY ")",
        last_temp, last_humid, last_code);
    } else {
      LOG_ERR("Weather: JSON error: %s", err.c_str());
    }
  } else {
    LOG_ERR("Weather: HTTP %d", code);
  }

  http.end();
}

void weather_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Weather " WEATHER_CITY);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  temp_label = lv_label_create(parent);
  lv_label_set_text(temp_label, "--.- C");
  lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(temp_label, lv_color_hex(0xffffff), 0);
  lv_obj_center(temp_label);
  lv_obj_set_y(temp_label, lv_obj_get_y(temp_label) - 24);

  cond_label = lv_label_create(parent);
  lv_label_set_text(cond_label, "---");
  lv_obj_set_style_text_font(cond_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(cond_label, lv_color_hex(0x00d4ff), 0);
  lv_obj_align_to(cond_label, temp_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  humid_label = lv_label_create(parent);
  lv_label_set_text(humid_label, "Humidity: --%");
  lv_obj_set_style_text_font(humid_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(humid_label, lv_color_hex(0x888899), 0);
  lv_obj_align_to(humid_label, cond_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  status_label = lv_label_create(parent);
  lv_label_set_text(status_label, "");
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x555566), 0);
  lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -26);

  // Don't auto-fetch on creation; let weather_update() handle it lazily
}

void weather_update(void) {
  if (!wifi_is_connected()) {
    lv_label_set_text(status_label, "WiFi off");
    return;
  }

  // Fetch if no data or cache expired
  if (!has_data || (millis() - last_fetch > WEATHER_REFRESH_MS)) {
    weather_fetch();
  }

  if (has_data) {
    int temp_int = (int)(last_temp * 10);
    lv_label_set_text_fmt(temp_label, "%d.%d C", temp_int / 10, abs(temp_int) % 10);
    lv_label_set_text_fmt(humid_label, "Humidity: %d%%", last_humid);
    lv_label_set_text(cond_label, wmo_code_str(last_code));

    unsigned long ago = (millis() - last_fetch) / 1000;
    char buf[32];
    if (ago < 60) snprintf(buf, sizeof(buf), "%lus ago", ago);
    else snprintf(buf, sizeof(buf), "%lumin ago", ago / 60);
    lv_label_set_text(status_label, buf);
  } else {
    lv_label_set_text(status_label, "Fetching...");
  }
}
