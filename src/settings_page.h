#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include <lvgl.h>

// Create the settings page UI
lv_obj_t* settings_page_create(void);

// Update settings page (called when page is visible)
void settings_page_update(void);

#endif // SETTINGS_PAGE_H
