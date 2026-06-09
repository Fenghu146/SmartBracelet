// NVS persistent storage using ESP32 Preferences library
#include "nvs_store.h"
#include <Preferences.h>
#include "debug_log.h"

static Preferences prefs;
static int cached_last_day = -1;

void nvs_store_init(void) {
    prefs.begin("smartbrace", false);  // namespace, read-write mode
    cached_last_day = prefs.getInt("last_day", -1);
    LOG_INFO("NVS: initialized (last_day=%d, steps_today=%d, steps_yesterday=%d)",
        cached_last_day,
        prefs.getInt("steps_today", 0),
        prefs.getInt("steps_yday", 0));
}

// ── Step counter ──
int nvs_get_steps_today(void) {
    return prefs.getInt("steps_today", 0);
}

void nvs_set_steps_today(int steps) {
    prefs.putInt("steps_today", steps);
}

int nvs_get_steps_yesterday(void) {
    return prefs.getInt("steps_yday", 0);
}

void nvs_set_steps_yesterday(int steps) {
    prefs.putInt("steps_yday", steps);
}

int nvs_get_last_day(void) {
    return cached_last_day;
}

void nvs_set_last_day(int day) {
    cached_last_day = day;
    prefs.putInt("last_day", day);
}

// ── User settings ──
int nvs_get_step_goal(void) {
    return prefs.getInt("step_goal", 8000);
}

void nvs_set_step_goal(int goal) {
    prefs.putInt("step_goal", goal);
}

int nvs_get_brightness(void) {
    return prefs.getInt("brightness", 100);
}

void nvs_set_brightness(int level) {
    prefs.putInt("brightness", level);
}

bool nvs_get_dnd(void) {
    return prefs.getBool("dnd", false);
}

void nvs_set_dnd(bool enable) {
    prefs.putBool("dnd", enable);
}

// ── WiFi config ──
bool nvs_get_wifi_configured(void) {
    String ssid = prefs.getString("wifi_ssid", "");
    return ssid.length() > 0;
}

void nvs_get_wifi_ssid(char *buf, int maxlen) {
    String s = prefs.getString("wifi_ssid", "");
    strncpy(buf, s.c_str(), maxlen - 1);
    buf[maxlen - 1] = '\0';
}

void nvs_set_wifi_ssid(const char *ssid) {
    prefs.putString("wifi_ssid", ssid);
}

void nvs_get_wifi_pass(char *buf, int maxlen) {
    String s = prefs.getString("wifi_pass", "");
    strncpy(buf, s.c_str(), maxlen - 1);
    buf[maxlen - 1] = '\0';
}

void nvs_set_wifi_pass(const char *pass) {
    prefs.putString("wifi_pass", pass);
}

// ── Daily reset ──
bool nvs_check_daily_reset(int current_day) {
    if (cached_last_day < 0) {
        // First boot - initialize
        nvs_set_last_day(current_day);
        return false;
    }
    if (current_day != cached_last_day) {
        // New day detected - rotate steps
        int today_steps = nvs_get_steps_today();
        nvs_set_steps_yesterday(today_steps);
        nvs_set_steps_today(0);
        nvs_set_last_day(current_day);
        LOG_INFO("NVS: daily reset. yesterday=%d steps", today_steps);
        return true;
    }
    return false;
}

// ── Crash recovery ──
void nvs_set_crash_page(int page) {
    prefs.putInt("crash_page", page);
}

int nvs_get_crash_page(void) {
    return prefs.getInt("crash_page", 0);
}

void nvs_set_crash_steps(int steps) {
    prefs.putInt("crash_steps", steps);
}

int nvs_get_crash_steps(void) {
    return prefs.getInt("crash_steps", 0);
}

// ── Watch face ──
void nvs_set_watch_face(int face) {
    prefs.putInt("watch_face", face);
}

int nvs_get_watch_face(void) {
    return prefs.getInt("watch_face", 0);
}

// ── Battery health ──
void nvs_set_batt_cycles(int cycles) {
    prefs.putInt("batt_cycles", cycles);
}

int nvs_get_batt_cycles(void) {
    return prefs.getInt("batt_cycles", 0);
}

void nvs_set_batt_full_mv(uint16_t mv) {
    prefs.putUShort("batt_full_mv", mv);
}

uint16_t nvs_get_batt_full_mv(void) {
    return prefs.getUShort("batt_full_mv", 4200);
}

// ── Weather location ──
void nvs_set_weather_lat(const char *lat) {
    prefs.putString("wx_lat", lat);
}

void nvs_get_weather_lat(char *buf, int maxlen) {
    String s = prefs.getString("wx_lat", "");
    strncpy(buf, s.c_str(), maxlen - 1);
    buf[maxlen - 1] = '\0';
}

void nvs_set_weather_lon(const char *lon) {
    prefs.putString("wx_lon", lon);
}

void nvs_get_weather_lon(char *buf, int maxlen) {
    String s = prefs.getString("wx_lon", "");
    strncpy(buf, s.c_str(), maxlen - 1);
    buf[maxlen - 1] = '\0';
}

bool nvs_has_weather_location(void) {
    String lat = prefs.getString("wx_lat", "");
    return lat.length() > 0;
}
