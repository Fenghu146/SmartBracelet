#ifndef BACKLIGHT_H
#define BACKLIGHT_H

// Initialize backlight PWM (call once in setup)
void backlight_init(void);

// Set backlight level: 0=off, 1-255=brightness
// level=0 turns off screen completely
void backlight_set_level(int level);

// Get current backlight level
int backlight_get_level(void);

// Convenience: on = saved brightness, off = 0
void backlight_on(void);
void backlight_off(void);

#endif // BACKLIGHT_H
