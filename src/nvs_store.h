#ifndef NVS_STORE_H
#define NVS_STORE_H

#include <stdint.h>
#include <stdbool.h>

// Initialize NVS storage
void nvs_store_init(void);

// Step counter persistence
int  nvs_get_steps_today(void);
void nvs_set_steps_today(int steps);
int  nvs_get_steps_yesterday(void);
void nvs_set_steps_yesterday(int steps);
int  nvs_get_last_day(void);  // day of month of last save
void nvs_set_last_day(int day);

// User settings
int  nvs_get_step_goal(void);
void nvs_set_step_goal(int goal);
int  nvs_get_brightness(void);  // 0-100
void nvs_set_brightness(int level);
bool nvs_get_dnd(void);
void nvs_set_dnd(bool enable);

// WiFi config
bool nvs_get_wifi_configured(void);
void nvs_get_wifi_ssid(char *buf, int maxlen);
void nvs_set_wifi_ssid(const char *ssid);
void nvs_get_wifi_pass(char *buf, int maxlen);
void nvs_set_wifi_pass(const char *pass);

// Daily reset check: returns true if a new day has started
bool nvs_check_daily_reset(int current_day);

// Crash recovery: save/restore page state
void nvs_set_crash_page(int page);
int  nvs_get_crash_page(void);
void nvs_set_crash_steps(int steps);
int  nvs_get_crash_steps(void);

// Watch face preference
void nvs_set_watch_face(int face);
int  nvs_get_watch_face(void);

// Battery health
void nvs_set_batt_cycles(int cycles);
int  nvs_get_batt_cycles(void);
void nvs_set_batt_full_mv(uint16_t mv);
uint16_t nvs_get_batt_full_mv(void);

#endif // NVS_STORE_H
