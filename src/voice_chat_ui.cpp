// Voice Chat LVGL page — local recording via INMP441 + desktop app ASR/AI
// State machine driven by voice_assistant module
// Optimized for 240x284 display: no emoji, clean layout, proper button sizing

#include "voice_chat_ui.h"
#include "service/voice_assistant.h"
#include <Arduino.h>
#include <lvgl.h>

LV_FONT_DECLARE(lv_font_simsun_16_cjk);

// UI elements
static lv_obj_t *record_btn = NULL;       // large circular button
static lv_obj_t *record_btn_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *progress_bar = NULL;     // LVGL bar for recording/sending progress
static lv_obj_t *trans_label = NULL;
static lv_obj_t *resp_label = NULL;
static lv_obj_t *retry_btn = NULL;

// For WAITING spinner — reset on state entry
static bool waiting_spinner_reset = false;

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
    case VA_RESPONSE:
    case VA_ERROR:
        va_dismiss();
        break;
    default:
        break;
    }
}

static void retry_btn_cb(lv_event_t *e) {
    (void)e;
    va_dismiss();
}

// -- Progress callback from voice_assistant (called from main loop) --
static void on_va_progress(float progress) {
    if (progress_bar && !lv_obj_has_flag(progress_bar, LV_OBJ_FLAG_HIDDEN)) {
        lv_bar_set_value(progress_bar, (int)(progress * 100), LV_ANIM_ON);
    }
}

// ============================================================
// Public API
// ============================================================

void voice_chat_page_create(lv_obj_t *parent) {
    // Dark background with column flex
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 12, 0);
    lv_obj_set_style_pad_row(parent, 4, 0);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "VOICE");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8888A0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);

    // Status label
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "Tap to Speak");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(status_label, LV_PCT(100));

    // Hint
    hint_label = lv_label_create(parent);
    lv_label_set_text(hint_label, "Press the mic button & speak");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_10, 0);

    // Progress bar (hidden by default)
    progress_bar = lv_bar_create(parent);
    lv_obj_set_size(progress_bar, lv_pct(85), 6);
    lv_obj_set_style_radius(progress_bar, 3, 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x00D488), LV_PART_INDICATOR);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);

    // Record button — big circle
    record_btn = lv_btn_create(parent);
    lv_obj_set_size(record_btn, 72, 72);
    lv_obj_set_style_radius(record_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x00D488), 0);
    lv_obj_set_style_border_color(record_btn, lv_color_hex(0x33FFAA), 0);
    lv_obj_set_style_border_width(record_btn, 2, 0);
    lv_obj_set_style_pad_all(record_btn, 0, 0);
    lv_obj_add_event_cb(record_btn, record_btn_cb, LV_EVENT_CLICKED, NULL);

    // Button icon / text
    record_btn_label = lv_label_create(record_btn);
    lv_label_set_text(record_btn_label, "MIC");
    lv_obj_set_style_text_font(record_btn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(record_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(record_btn_label);

    // Transcription header
    lv_obj_t *trans_hdr = lv_label_create(parent);
    lv_label_set_text(trans_hdr, "You:");
    lv_obj_set_style_text_color(trans_hdr, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(trans_hdr, &lv_font_montserrat_10, 0);

    // Transcription box
    lv_obj_t *trans_box = lv_obj_create(parent);
    lv_obj_set_size(trans_box, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(trans_box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(trans_box, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_border_width(trans_box, 1, 0);
    lv_obj_set_style_radius(trans_box, 6, 0);
    lv_obj_set_style_pad_all(trans_box, 4, 0);

    trans_label = lv_label_create(trans_box);
    lv_label_set_text(trans_label, "");
    lv_obj_set_style_text_color(trans_label, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_font(trans_label, &lv_font_simsun_16_cjk, 0);
    lv_label_set_long_mode(trans_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(trans_label, LV_PCT(100));

    // Response header
    lv_obj_t *resp_hdr = lv_label_create(parent);
    lv_label_set_text(resp_hdr, "AI:");
    lv_obj_set_style_text_color(resp_hdr, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(resp_hdr, &lv_font_montserrat_10, 0);

    // Response box
    lv_obj_t *resp_box = lv_obj_create(parent);
    lv_obj_set_size(resp_box, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(resp_box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(resp_box, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_border_width(resp_box, 1, 0);
    lv_obj_set_style_radius(resp_box, 6, 0);
    lv_obj_set_style_pad_all(resp_box, 4, 0);
    lv_obj_set_scroll_dir(resp_box, LV_DIR_VER);

    resp_label = lv_label_create(resp_box);
    lv_label_set_text(resp_label, "");
    lv_obj_set_style_text_color(resp_label, lv_color_hex(0xEEEFF6), 0);
    lv_obj_set_style_text_font(resp_label, &lv_font_simsun_16_cjk, 0);
    lv_label_set_long_mode(resp_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(resp_label, LV_PCT(100));

    // Retry button
    retry_btn = lv_btn_create(parent);
    lv_obj_set_size(retry_btn, 90, 28);
    lv_obj_set_style_radius(retry_btn, 14, 0);
    lv_obj_set_style_bg_color(retry_btn, lv_color_hex(0xFF4466), 0);
    lv_obj_add_flag(retry_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(retry_btn, retry_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *retry_label = lv_label_create(retry_btn);
    lv_label_set_text(retry_label, "TRY AGAIN");
    lv_obj_set_style_text_font(retry_label, &lv_font_montserrat_12, 0);
    lv_obj_center(retry_label);
    lv_obj_set_style_text_color(retry_label, lv_color_hex(0xFFFFFF), 0);

    // Register progress callback
    va_set_progress_callback(on_va_progress);
}

void voice_chat_page_update(void) {
    if (!record_btn) return;

    va_state_t state = va_get_state();

    // Default: hide conditional elements
    lv_obj_add_flag(retry_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(record_btn, LV_OBJ_FLAG_HIDDEN);

    switch (state) {

    case VA_IDLE:
        lv_label_set_text(record_btn_label, "MIC");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x00D488), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0x33FFAA), 0);
        lv_label_set_text(status_label, "Tap to Speak");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
        lv_obj_clear_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(hint_label, "Press the mic button & speak");
        lv_label_set_text(trans_label, "");
        lv_label_set_text(resp_label, "");
        break;

    case VA_RECORDING:
        lv_label_set_text(record_btn_label, "STOP");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xFF4466), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0xFF8888), 0);
        lv_label_set_text(status_label, "Recording...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4466), 0);
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        // Show progress bar for recording duration
        lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(progress_bar, (int)(va_get_progress() * 100), LV_ANIM_ON);
        break;

    case VA_SENDING:
        lv_label_set_text(record_btn_label, "WAIT");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xFFAA00), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0xFFCC66), 0);
        lv_label_set_text(status_label, "Sending...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        // Progress bar for sending progress
        lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(progress_bar, (int)(va_get_progress() * 100), LV_ANIM_ON);
        break;

    case VA_WAITING:
        lv_obj_add_flag(record_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_label, "Processing...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
        // Spinning progress
        lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        {
            static unsigned long last_spin = 0;
            static int spin_val = 0;
            unsigned long now = millis();
            if (now - last_spin > 80) {
                last_spin = now;
                spin_val = (spin_val + 8) % 100;
                lv_bar_set_value(progress_bar, spin_val, LV_ANIM_ON);
            }
        }
        break;

    case VA_RESPONSE:
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_label, "Done");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D4FF), 0);
        // Show result text
        {
            const char *trans = va_get_transcription();
            const char *resp = va_get_response();
            if (trans && trans[0]) lv_label_set_text(trans_label, trans);
            if (resp && resp[0]) lv_label_set_text(resp_label, resp);
        }
        // Change button to dismiss
        lv_label_set_text(record_btn_label, "OK");
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x555575), 0);
        lv_obj_set_style_border_color(record_btn, lv_color_hex(0x8888A0), 0);
        break;

    case VA_ERROR:
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_label, "Error");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4466), 0);
        lv_label_set_text(trans_label, "");
        {
            const char *err = va_get_error_msg();
            if (err && err[0]) lv_label_set_text(resp_label, err);
        }
        // Show retry button, hide record button
        lv_obj_add_flag(record_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(retry_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}
