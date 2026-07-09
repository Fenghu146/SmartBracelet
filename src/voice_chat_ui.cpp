// Voice Chat LVGL page — simple trigger button
// Minimal UI: single button to start/stop voice recording
// State machine driven by voice_assistant module

#include "voice_chat_ui.h"
#include "service/voice_assistant.h"
#include <Arduino.h>
#include <lvgl.h>

// UI elements
static lv_obj_t *record_btn = NULL;       // large circular trigger button
static lv_obj_t *record_btn_label = NULL;
static lv_obj_t *status_label = NULL;     // one-line status below button

// -- Button event handler --
static void record_btn_cb(lv_event_t *e) {
    (void)e;
    switch (va_get_state()) {
    case VA_IDLE:
        va_start_recording();
        break;
    case VA_RECORDING:
        va_stop_recording();
        break;
    case VA_SENDING:
    case VA_WAITING:
        // Ignore during processing
        break;
    case VA_RESPONSE:
    case VA_ERROR:
        va_dismiss();
        break;
    default:
        break;
    }
}

// ============================================================
// Public API
// ============================================================

void voice_chat_page_create(lv_obj_t *parent) {
    // Dark background, column layout centered
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 12, 0);
    lv_obj_set_style_pad_row(parent, 16, 0);

    // Spacer to push button toward center
    lv_obj_t *spacer_top = lv_obj_create(parent);
    lv_obj_set_size(spacer_top, 1, 1);
    lv_obj_set_style_border_width(spacer_top, 0, 0);
    lv_obj_set_style_bg_opa(spacer_top, LV_OPA_TRANSP, 0);

    // Record button — big circle, main trigger
    record_btn = lv_btn_create(parent);
    lv_obj_set_size(record_btn, 80, 80);
    lv_obj_set_style_radius(record_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x00D488), 0);
    lv_obj_set_style_border_color(record_btn, lv_color_hex(0x33FFAA), 0);
    lv_obj_set_style_border_width(record_btn, 2, 0);
    lv_obj_set_style_pad_all(record_btn, 0, 0);
    lv_obj_add_event_cb(record_btn, record_btn_cb, LV_EVENT_CLICKED, NULL);

    // Button label
    record_btn_label = lv_label_create(record_btn);
    lv_label_set_text(record_btn_label, "\xEE\x80\x80");   // default icon/char
    lv_obj_set_style_text_font(record_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(record_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(record_btn_label);

    // Status label (below button)
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "Tap to Speak");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x8888A0), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    // Spacer to balance layout
    lv_obj_t *spacer_bot = lv_obj_create(parent);
    lv_obj_set_size(spacer_bot, 1, 1);
    lv_obj_set_style_border_width(spacer_bot, 0, 0);
    lv_obj_set_style_bg_opa(spacer_bot, LV_OPA_TRANSP, 0);
}

void voice_chat_page_update(void) {
    if (!record_btn) return;

    va_state_t state = va_get_state();

    switch (state) {

    case VA_IDLE:
        lv_label_set_text(record_btn_label, "MIC");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x00D488), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0x33FFAA), 0);
        lv_label_set_text(status_label, "Tap to Speak");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x8888A0), 0);
        break;

    case VA_RECORDING:
        lv_label_set_text(record_btn_label, "STOP");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xFF4466), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0xFF8888), 0);
        lv_label_set_text(status_label, "Recording...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4466), 0);
        break;

    case VA_SENDING:
        lv_label_set_text(record_btn_label, "SND");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xFFAA00), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0xFFCC66), 0);
        lv_label_set_text(status_label, "Sending...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
        break;

    case VA_WAITING:
        lv_label_set_text(record_btn_label, "…");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xFFAA00), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0xFFCC66), 0);
        lv_label_set_text(status_label, "Processing...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
        break;

    case VA_RESPONSE:
        lv_label_set_text(record_btn_label, "OK");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x555575), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0x8888A0), 0);
        lv_label_set_text(status_label, "Done — tap to dismiss");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
        break;

    case VA_ERROR:
        lv_label_set_text(record_btn_label, "!");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xFF4466), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0xFF8888), 0);
        lv_label_set_text(status_label, "Error — tap to retry");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4466), 0);
        break;
    }
}
