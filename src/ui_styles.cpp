// Shared LVGL style definitions
#include "ui_styles.h"

lv_style_t style_page_bg;
lv_style_t style_title;
lv_style_t style_label_primary;
lv_style_t style_label_secondary;
lv_style_t style_label_muted;
lv_style_t style_label_accent;
lv_style_t style_btn_dark;

void ui_styles_init(void) {
    // Page background
    lv_style_init(&style_page_bg);
    lv_style_set_bg_color(&style_page_bg, lv_color_hex(COLOR_BG_PRIMARY));
    lv_style_set_bg_opa(&style_page_bg, LV_OPA_COVER);
    lv_style_set_border_width(&style_page_bg, 0);
    lv_style_set_pad_all(&style_page_bg, 0);
    lv_style_set_radius(&style_page_bg, 0);

    // Title text
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_16);
    lv_style_set_text_color(&style_title, lv_color_hex(COLOR_ACCENT));

    // Primary text
    lv_style_init(&style_label_primary);
    lv_style_set_text_font(&style_label_primary, &lv_font_montserrat_14);
    lv_style_set_text_color(&style_label_primary, lv_color_hex(COLOR_TEXT_PRIMARY));

    // Secondary text
    lv_style_init(&style_label_secondary);
    lv_style_set_text_font(&style_label_secondary, &lv_font_montserrat_12);
    lv_style_set_text_color(&style_label_secondary, lv_color_hex(COLOR_TEXT_SECONDARY));

    // Muted text
    lv_style_init(&style_label_muted);
    lv_style_set_text_font(&style_label_muted, &lv_font_montserrat_10);
    lv_style_set_text_color(&style_label_muted, lv_color_hex(COLOR_TEXT_DIM));

    // Accent text
    lv_style_init(&style_label_accent);
    lv_style_set_text_font(&style_label_accent, &lv_font_montserrat_16);
    lv_style_set_text_color(&style_label_accent, lv_color_hex(COLOR_ACCENT));

    // Dark button
    lv_style_init(&style_btn_dark);
    lv_style_set_bg_color(&style_btn_dark, lv_color_hex(COLOR_BG_CARD));
    lv_style_set_bg_opa(&style_btn_dark, LV_OPA_COVER);
    lv_style_set_radius(&style_btn_dark, 4);
}
