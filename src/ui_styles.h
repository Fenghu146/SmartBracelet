#ifndef UI_STYLES_H
#define UI_STYLES_H

#include <lvgl.h>

// Shared colors used across all pages
#define COLOR_BG_PRIMARY    0x0d0d1a
#define COLOR_BG_CARD       0x1a1a2e
#define COLOR_ACCENT        0x00d4ff
#define COLOR_GREEN         0x00d488
#define COLOR_RED           0xff4444
#define COLOR_AMBER         0xffaa00
#define COLOR_TEXT_PRIMARY  0xffffff
#define COLOR_TEXT_SECONDARY 0xcccccc
#define COLOR_TEXT_MUTED    0x888899
#define COLOR_TEXT_DIM      0x555566

// Shared LVGL style instances (initialized once)
extern lv_style_t style_page_bg;
extern lv_style_t style_title;
extern lv_style_t style_label_primary;
extern lv_style_t style_label_secondary;
extern lv_style_t style_label_muted;
extern lv_style_t style_label_accent;
extern lv_style_t style_btn_dark;

// Initialize all shared styles (call once during setup)
void ui_styles_init(void);

// Apply page background style to an object
static inline void apply_page_bg(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_BG_PRIMARY), 0);
}

// Create a styled title label
static inline lv_obj_t* create_title(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);
    return lbl;
}

// Create a styled button (dark background)
static inline lv_obj_t* create_dark_btn(lv_obj_t *parent, const char *text,
    int w, int h, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BG_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

#endif // UI_STYLES_H
