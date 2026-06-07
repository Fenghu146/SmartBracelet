#ifndef QUICK_PANEL_H
#define QUICK_PANEL_H

#include <lvgl.h>

// Create the quick panel overlay (call once in setup)
void quick_panel_init(void);

// Show/hide the quick panel
void quick_panel_show(void);
void quick_panel_hide(void);
bool quick_panel_is_visible(void);

// Update panel data (time, battery)
void quick_panel_update(int hour, int minute, int batt_pct, bool wifi_on, bool ble_on);

#endif // QUICK_PANEL_H
