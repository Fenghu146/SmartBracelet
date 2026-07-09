#ifndef UI_PAGES_H
#define UI_PAGES_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct CST816S;

// Page index enum - replaces magic numbers throughout codebase
enum PageIndex {
    PAGE_DIGITAL = 0,
    PAGE_ANALOG,
    PAGE_SENSOR,
    PAGE_NOTIF,
    PAGE_STOPWATCH,
    PAGE_WEATHER,
    PAGE_VOICE,
    PAGE_SETTINGS,
    PAGE_COUNT
};

// Telemetry struct for passing hardware state to UI
typedef struct {
    int hour, minute, second, day, month, year, week;
    int batt_percent;
    uint16_t batt_mv;
    bool batt_valid;
    bool charging;
    bool usb_powered;
    int step_count;
    bool wifi_connected;
    float acc_x, acc_y, acc_z;
    float gyr_x, gyr_y, gyr_z;
    uint8_t intensity;      // 0-100
    float mets;             // METs
    float calories;         // kcal since daily reset
} ui_telemetry_t;

// Initialize all pages and status bar
void ui_pages_init(lv_obj_t **pages, int num_pages, CST816S *touch_dev);

// Page update functions
void ui_update_watchface(const ui_telemetry_t *t);
void ui_update_analog(int hour, int minute, int second);
void ui_update_sensor_page(const ui_telemetry_t *t);
void ui_update_notif_page(void);

// Accessors
int  ui_get_num_pages(void);
void ui_set_step_label(int steps);

#endif // UI_PAGES_H
