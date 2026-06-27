// Quick control panel: overlay with toggles and brightness slider
#include "quick_panel.h"
#include "backlight.h"
#include "nvs_store.h"
#include "service/wifi_ntp.h"
#include "debug_log.h"
#include "pin_config.h"
#include <stdio.h>

static lv_obj_t *qp_overlay = nullptr;
static lv_obj_t *qp_container = nullptr;
static lv_obj_t *qp_time = nullptr;
static lv_obj_t *qp_batt = nullptr;
static lv_obj_t *qp_wifi_btn = nullptr;
static lv_obj_t *qp_dnd_btn = nullptr;
static lv_obj_t *qp_wifi_lbl = nullptr;
static lv_obj_t *qp_dnd_lbl = nullptr;
static lv_obj_t *qp_slider = nullptr;
static lv_obj_t *qp_slider_lbl = nullptr;
static bool qp_visible = false;
static bool qp_wifi = false;
static bool qp_dnd = false;

static void qp_overlay_click(lv_event_t *e) {
    if (lv_event_get_target(e) == qp_overlay) quick_panel_hide();
}

// Helper: update toggle button colors based on state
static void update_toggle_style(lv_obj_t *btn, lv_obj_t *lbl, bool on) {
    lv_obj_set_style_bg_color(btn, on ? lv_color_hex(0x00d4ff) : lv_color_hex(0x2a2a45), 0);
    lv_obj_set_style_text_color(lbl, on ? lv_color_hex(0x000000) : lv_color_hex(0x888899), 0);
}

static void qp_mk_btn(lv_obj_t *par, lv_obj_t **btn, lv_obj_t **lbl,
    const char *txt, bool on, int x, int y, lv_event_cb_t cb)
{
    *btn = lv_btn_create(par);
    lv_obj_set_size(*btn, 64, 48);
    lv_obj_align(*btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(*btn, on ? lv_color_hex(0x00d4ff) : lv_color_hex(0x2a2a45), 0);
    lv_obj_set_style_bg_opa(*btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(*btn, 10, 0);
    *lbl = lv_label_create(*btn);
    lv_label_set_text(*lbl, txt);
    lv_obj_set_style_text_font(*lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(*lbl, on ? lv_color_hex(0x000000) : lv_color_hex(0x888899), 0);
    lv_obj_center(*lbl);
    if (cb) lv_obj_add_event_cb(*btn, cb, LV_EVENT_CLICKED, NULL);
}

static void qp_on_wifi(lv_event_t *e) {
    qp_wifi = !qp_wifi;
    if (qp_wifi) wifi_power_on(); else wifi_power_off();
    update_toggle_style(qp_wifi_btn, qp_wifi_lbl, qp_wifi);
}

static void qp_on_dnd(lv_event_t *e) {
    qp_dnd = !qp_dnd;
    nvs_set_dnd(qp_dnd);
    nvs_set_dnd(qp_dnd);
    lv_obj_set_style_bg_color(qp_dnd_btn, qp_dnd ? lv_color_hex(0xffaa00) : lv_color_hex(0x2a2a45), 0);
    lv_obj_set_style_text_color(qp_dnd_lbl, qp_dnd ? lv_color_hex(0x000000) : lv_color_hex(0x888899), 0);
    lv_label_set_text(qp_dnd_lbl, qp_dnd ? "DND ON" : "DND");
}

static void qp_on_slider(lv_event_t *e) {
    int v = lv_slider_get_value(qp_slider);
    nvs_set_brightness(v);
    backlight_set_level((v * 255) / 100);
    char b[16]; snprintf(b, sizeof(b), "%d%%", v);
    lv_label_set_text(qp_slider_lbl, b);
}

void quick_panel_init(void) {
    qp_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(qp_overlay);
    lv_obj_set_size(qp_overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(qp_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(qp_overlay, LV_OPA_50, 0);
    lv_obj_add_flag(qp_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(qp_overlay, qp_overlay_click, LV_EVENT_CLICKED, NULL);

    qp_container = lv_obj_create(qp_overlay);
    lv_obj_remove_style_all(qp_container);
    lv_obj_set_size(qp_container, LCD_WIDTH - 16, 200);
    lv_obj_align(qp_container, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(qp_container, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(qp_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qp_container, 12, 0);
    lv_obj_set_style_pad_all(qp_container, 12, 0);
    lv_obj_add_flag(qp_container, LV_OBJ_FLAG_CLICKABLE);

    qp_time = lv_label_create(qp_container);
    lv_label_set_text(qp_time, "00:00");
    lv_obj_set_style_text_font(qp_time, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(qp_time, lv_color_hex(0xffffff), 0);
    lv_obj_align(qp_time, LV_ALIGN_TOP_LEFT, 0, 0);

    qp_batt = lv_label_create(qp_container);
    lv_label_set_text(qp_batt, "--%");
    lv_obj_set_style_text_font(qp_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qp_batt, lv_color_hex(0x00d488), 0);
    lv_obj_align(qp_batt, LV_ALIGN_TOP_RIGHT, 0, 2);

    qp_dnd = nvs_get_dnd();
    qp_wifi = wifi_is_powered();

    qp_mk_btn(qp_container, &qp_wifi_btn, &qp_wifi_lbl, "WiFi", qp_wifi, 0, 30, qp_on_wifi);
    qp_mk_btn(qp_container, &qp_dnd_btn, &qp_dnd_lbl, qp_dnd ? "DND ON" : "DND", qp_dnd, 72, 30, qp_on_dnd);

    lv_obj_t *st = lv_label_create(qp_container);
    lv_label_set_text(st, "Brightness");
    lv_obj_set_style_text_font(st, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(st, lv_color_hex(0x888899), 0);
    lv_obj_align(st, LV_ALIGN_TOP_LEFT, 0, 88);

    qp_slider_lbl = lv_label_create(qp_container);
    int cp = nvs_get_brightness();
    char sb[16]; snprintf(sb, sizeof(sb), "%d%%", cp);
    lv_label_set_text(qp_slider_lbl, sb);
    lv_obj_set_style_text_font(qp_slider_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(qp_slider_lbl, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(qp_slider_lbl, LV_ALIGN_TOP_RIGHT, 0, 88);

    qp_slider = lv_slider_create(qp_container);
    lv_slider_set_range(qp_slider, 10, 100);
    lv_slider_set_value(qp_slider, cp, LV_ANIM_OFF);
    lv_obj_set_width(qp_slider, LV_PCT(100));
    lv_obj_align(qp_slider, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_color(qp_slider, lv_color_hex(0x2a2a45), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qp_slider, lv_color_hex(0x00d4ff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(qp_slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_pad_all(qp_slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(qp_slider, qp_on_slider, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *h = lv_label_create(qp_container);
    lv_label_set_text(h, "Tap outside to close");
    lv_obj_set_style_text_font(h, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x555566), 0);
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void quick_panel_show(void) {
    if (!qp_visible) { lv_obj_clear_flag(qp_overlay, LV_OBJ_FLAG_HIDDEN); qp_visible = true; }
}

void quick_panel_hide(void) {
    if (qp_visible) { lv_obj_add_flag(qp_overlay, LV_OBJ_FLAG_HIDDEN); qp_visible = false; }
}

bool quick_panel_is_visible(void) { return qp_visible; }

void quick_panel_update(int hour, int minute, int batt_pct, bool wifi_on, bool ble_on) {
    (void)ble_on;
    if (!qp_visible) return;
    char ts[8]; snprintf(ts, sizeof(ts), "%02d:%02d", hour, minute);
    lv_label_set_text(qp_time, ts);
    char bs[8]; snprintf(bs, sizeof(bs), "%d%%", batt_pct);
    lv_label_set_text(qp_batt, bs);
    qp_wifi = wifi_on;
    update_toggle_style(qp_wifi_btn, qp_wifi_lbl, qp_wifi);
}
