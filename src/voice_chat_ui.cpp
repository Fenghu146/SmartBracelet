// Voice Chat LVGL page â€?displays conversation from phone
// Phone records audio + calls cloud APIs, watch just shows results

#include "voice_chat_ui.h"
#include "service/voice_chat.h"
#include "service/ble_srv.h"
#include <lvgl.h>

LV_FONT_DECLARE(lv_font_simsun_16_cjk);

static lv_obj_t *status_label = NULL;
static lv_obj_t *trans_label = NULL;
static lv_obj_t *resp_label = NULL;
static lv_obj_t *hint_label = NULL;

void voice_chat_page_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Voice Chat");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8888A0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);

    // Status label
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(status_label, LV_PCT(100));

    // Hint
    hint_label = lv_label_create(parent);
    lv_label_set_text(hint_label, "Open phone app to start");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);

    // Transcription box
    lv_obj_t *trans_hdr = lv_label_create(parent);
    lv_label_set_text(trans_hdr, "You said:");
    lv_obj_set_style_text_color(trans_hdr, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(trans_hdr, &lv_font_montserrat_10, 0);

    lv_obj_t *trans_box = lv_obj_create(parent);
    lv_obj_set_size(trans_box, LV_PCT(100), 45);
    lv_obj_set_style_bg_color(trans_box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(trans_box, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_radius(trans_box, 8, 0);
    lv_obj_set_style_pad_all(trans_box, 8, 0);

    trans_label = lv_label_create(trans_box);
    lv_label_set_text(trans_label, "...");
    lv_obj_set_style_text_color(trans_label, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_font(trans_label, &lv_font_simsun_16_cjk, 0);
    lv_label_set_long_mode(trans_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(trans_label, LV_PCT(100));

    // Response box
    lv_obj_t *resp_hdr = lv_label_create(parent);
    lv_label_set_text(resp_hdr, "AI says:");
    lv_obj_set_style_text_color(resp_hdr, lv_color_hex(0x555570), 0);
    lv_obj_set_style_text_font(resp_hdr, &lv_font_montserrat_10, 0);

    lv_obj_t *resp_box = lv_obj_create(parent);
    lv_obj_set_size(resp_box, LV_PCT(100), 80);
    lv_obj_set_style_bg_color(resp_box, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(resp_box, lv_color_hex(0x2A2A45), 0);
    lv_obj_set_style_radius(resp_box, 8, 0);
    lv_obj_set_style_pad_all(resp_box, 8, 0);
    lv_obj_set_scroll_dir(resp_box, LV_DIR_VER);

    resp_label = lv_label_create(resp_box);
    lv_label_set_text(resp_label, "");
    lv_obj_set_style_text_color(resp_label, lv_color_hex(0xEEEFF6), 0);
    lv_obj_set_style_text_font(resp_label, &lv_font_simsun_16_cjk, 0);
    lv_label_set_long_mode(resp_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(resp_label, LV_PCT(100));
}

void voice_chat_page_update(void) {
    if (!status_label) return;

    voice_state_t state = voice_chat_get_state();
    switch (state) {
        case VOICE_IDLE:
            lv_label_set_text(status_label, "Ready");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D488), 0);
            lv_obj_clear_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case VOICE_RECORDING:
            lv_label_set_text(status_label, "Recording...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4466), 0);
            lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case VOICE_WAITING:
            lv_label_set_text(status_label, "Processing...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
            lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case VOICE_RESPONSE: {
            const char *trans = voice_chat_get_transcription();
            const char *resp = voice_chat_get_response();
            if (trans && trans[0]) lv_label_set_text(trans_label, trans);
            if (resp && resp[0]) lv_label_set_text(resp_label, resp);
            lv_label_set_text(status_label, "Done");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D4FF), 0);
            lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
            break;
        }
    }
}
