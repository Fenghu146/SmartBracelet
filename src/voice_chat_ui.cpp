// Voice Chat LVGL page — 9th page on the watch
// Layout: status + transcription + response + PTT button

#include "voice_chat_ui.h"
#include "service/voice_chat.h"
#include <lvgl.h>

static lv_obj_t *status_label = NULL;
static lv_obj_t *trans_label = NULL;
static lv_obj_t *resp_label = NULL;
static lv_obj_t *ptt_btn = NULL;
static lv_obj_t *ptt_label = NULL;
static bool ptt_pressed = false;

// PTT button press/release handlers
static void ptt_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ptt_pressed = true;
        lv_obj_set_style_bg_color(ptt_btn, lv_color_hex(0xFF4466), 0);
        lv_label_set_text(ptt_label, "...");
        voice_chat_start_recording();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESSING) {
        if (code == LV_EVENT_RELEASED && ptt_pressed) {
            ptt_pressed = false;
            lv_obj_set_style_bg_color(ptt_btn, lv_color_hex(0x00D4FF), 0);
            voice_chat_stop_recording();
        }
    }
}

void voice_chat_page_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 12, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Voice Chat");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8888A0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_NONE, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);

    // Status label
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(status_label, LV_PCT(100));

    // Transcription box
    lv_obj_t *trans_box = lv_obj_create(parent);
    lv_obj_set_size(trans_box, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(trans_box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(trans_box, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_radius(trans_box, 8, 0);
    lv_obj_set_style_pad_all(trans_box, 8, 0);
    lv_obj_set_scroll_dir(trans_box, LV_DIR_VER);

    trans_label = lv_label_create(trans_box);
    lv_label_set_text(trans_label, "Say something...");
    lv_obj_set_style_text_color(trans_label, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(trans_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(trans_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(trans_label, LV_PCT(100));

    // Response box
    lv_obj_t *resp_box = lv_obj_create(parent);
    lv_obj_set_size(resp_box, LV_PCT(100), 70);
    lv_obj_set_style_bg_color(resp_box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(resp_box, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_radius(resp_box, 8, 0);
    lv_obj_set_style_pad_all(resp_box, 8, 0);
    lv_obj_set_scroll_dir(resp_box, LV_DIR_VER);

    resp_label = lv_label_create(resp_box);
    lv_label_set_text(resp_label, "");
    lv_obj_set_style_text_color(resp_label, lv_color_hex(0xEEEFF6), 0);
    lv_obj_set_style_text_font(resp_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(resp_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(resp_label, LV_PCT(100));

    // Spacer
    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    // PTT button
    ptt_btn = lv_btn_create(parent);
    lv_obj_set_size(ptt_btn, 70, 70);
    lv_obj_set_style_bg_color(ptt_btn, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_radius(ptt_btn, 35, 0);
    lv_obj_set_style_shadow_width(ptt_btn, 20, 0);
    lv_obj_set_style_shadow_color(ptt_btn, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_shadow_opa(ptt_btn, LV_OPA_40, 0);
    lv_obj_add_event_cb(ptt_btn, ptt_event_cb, LV_EVENT_ALL, NULL);

    ptt_label = lv_label_create(ptt_btn);
    lv_label_set_text(ptt_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(ptt_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(ptt_label, &lv_font_montserrat_24, 0);
    lv_obj_center(ptt_label);
}

void voice_chat_page_update(void) {
    if (!status_label) return;

    voice_state_t state = voice_chat_get_state();
    switch (state) {
        case VOICE_IDLE:
            lv_label_set_text(status_label, "Ready");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
            if (!ptt_pressed) lv_label_set_text(ptt_label, LV_SYMBOL_AUDIO);
            break;
        case VOICE_RECORDING:
            lv_label_set_text(status_label, "Recording...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4466), 0);
            lv_label_set_text(ptt_label, LV_SYMBOL_STOP);
            break;
        case VOICE_SENDING:
            lv_label_set_text(status_label, "Sending...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
            break;
        case VOICE_WAITING:
            lv_label_set_text(status_label, "Processing...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
            break;
        case VOICE_RESPONSE: {
            const char *trans = voice_chat_get_transcription();
            const char *resp = voice_chat_get_response();
            if (trans && trans[0]) lv_label_set_text(trans_label, trans);
            if (resp && resp[0]) lv_label_set_text(resp_label, resp);
            lv_label_set_text(status_label, "Done");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D4FF), 0);
            break;
        }
        case VOICE_PLAYING_TTS:
            lv_label_set_text(status_label, "Playing...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D4FF), 0);
            break;
    }
}
