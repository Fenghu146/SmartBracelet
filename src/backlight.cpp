// Backlight PWM control + screen state management
#include "backlight.h"
#include "nvs_store.h"
#include "sensor_task.h"
#include "pin_config.h"
#include <Arduino.h>
#include <esp_system.h>

#define BL_PWM_CH    0
#define BL_PWM_FREQ  5000
#define BL_PWM_RES   8   // 0-255

static int bl_current_level = 255;
static bool screen_on_state = true;

void backlight_init(void) {
    ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_RES);
    ledcAttachPin(LCD_BL, BL_PWM_CH);
    // Apply saved brightness
    int pct = nvs_get_brightness();
    bl_current_level = (pct * 255) / 100;
    if (bl_current_level < 10) bl_current_level = 10;
    ledcWrite(BL_PWM_CH, bl_current_level);
}

void backlight_set_level(int level) {
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    bl_current_level = level;
    ledcWrite(BL_PWM_CH, level);
}

int backlight_get_level(void) {
    return bl_current_level;
}

void backlight_on(void) {
    int pct = nvs_get_brightness();
    int level = (pct * 255) / 100;
    if (level < 10) level = 10;
    backlight_set_level(level);
    screen_on_state = true;
}

void backlight_off(void) {
    backlight_set_level(0);
    screen_on_state = false;
}

bool screen_is_on(void) {
    return screen_on_state;
}

void set_backlight(bool on) {
    if (on) {
        backlight_on();
        // Restore full performance
        sensor_task_set_rate(125);
        setCpuFrequencyMhz(240);
    } else {
        backlight_off();
        // Enter low power mode
        setCpuFrequencyMhz(80);
        sensor_task_set_rate(25);
    }
}
