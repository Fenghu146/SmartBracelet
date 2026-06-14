#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <stdbool.h>

// Initialize backlight PWM (call once in setup)
void backlight_init(void);

// Set backlight level: 0=off, 1-255=brightness
void backlight_set_level(int level);

// Get current backlight level
int backlight_get_level(void);

// Convenience: on = saved brightness, off = 0
void backlight_on(void);
void backlight_off(void);

// Screen state query
bool screen_is_on(void);

// Set backlight + CPU/sensor power mode
void set_backlight(bool on);

#endif // BACKLIGHT_H
