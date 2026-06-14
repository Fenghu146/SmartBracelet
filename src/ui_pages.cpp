// UI pages: creation and update logic extracted from main.cpp
#include "ui_pages.h"
#include "pin_config.h"
#include "debug_log.h"
#include "ui_styles.h"
#include "stopwatch.h"
#include "weather.h"
#include "activity.h"
#include "player.h"
#include "service/audio.h"
#include "service/voice_chat.h"
#include "voice_chat_ui.h"
#include "service/ble_hid.h"
#include "service/ble_srv.h"
#include "notif_history.h"
#include <math.h>

static const int CX = 120, CY = 142; // screen center

// 鈹€鈹€ Status bar objects 鈹€鈹€
static lv_obj_t *wifi_icon = nullptr;
static lv_obj_t *ble_icon = nullptr;
static lv_obj_t *battery_bar_outer = nullptr;
static lv_obj_t *battery_bar = nullptr;
static lv_obj_t *battery_label = nullptr;
static lv_obj_t *charging_label = nullptr;
static lv_obj_t *page_dots = nullptr;

// 鈹€鈹€ Watchface objects 鈹€鈹€
static lv_obj_t *time_label = nullptr;
static lv_obj_t *date_label = nullptr;
static lv_obj_t *step_label = nullptr;

// 鈹€鈹€ Sensor page objects 鈹€鈹€
static lv_obj_t *accel_label = nullptr;
static lv_obj_t *gyro_label = nullptr;
static lv_obj_t *batt_volt_label = nullptr;
static lv_obj_t *intensity_label = nullptr;
static lv_obj_t *calories_label = nullptr;
static lv_obj_t *sleep_label = nullptr;

// 鈹€鈹€ Analog watchface objects 鈹€鈹€
static lv_obj_t *analog_face = nullptr;
static lv_obj_t *hour_hand = nullptr;
static lv_obj_t *min_hand = nullptr;
static lv_obj_t *sec_hand = nullptr;
static lv_point_t hour_pts[2], min_pts[2], sec_pts[2];
static lv_point_t dial_pts[12][2];
static int prev_hour = -1, prev_min = -1, prev_sec = -1;
static bool analog_inited = false;
static lv_obj_t *dial_marks[12];

// 鈹€鈹€ Notification page objects 鈹€鈹€
static lv_obj_t *notif_list = nullptr;
static lv_obj_t *notif_empty_label = nullptr;

static int last_batt = -1;
static int last_notif_count = -1;  // Track notification count for incremental update
static char time_str[12], date_str[32];

// 鈹€鈹€ Status bar 鈹€鈹€
static void status_bar_create(lv_obj_t *parent) {
    wifi_icon = lv_label_create(parent);
    lv_label_set_text(wifi_icon, "~");
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_LEFT, 16, 6);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x555566), 0);

    ble_icon = lv_label_create(parent);
    lv_label_set_text(ble_icon, ")");
    lv_obj_align(ble_icon, LV_ALIGN_TOP_LEFT, 32, 6);
    lv_obj_set_style_text_font(ble_icon, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ble_icon, lv_color_hex(0x555566), 0);

    // Battery icon bar (right side)
    battery_bar_outer = lv_obj_create(parent);
    lv_obj_remove_style_all(battery_bar_outer);
    lv_obj_set_size(battery_bar_outer, 24, 10);
    lv_obj_set_style_border_width(battery_bar_outer, 1, 0);
    lv_obj_set_style_border_color(battery_bar_outer, lv_color_hex(0x888899), 0);
    lv_obj_set_style_radius(battery_bar_outer, 1, 0);
    lv_obj_set_style_pad_all(battery_bar_outer, 1, 0);
    lv_obj_set_style_bg_color(battery_bar_outer, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(battery_bar_outer, LV_OPA_COVER, 0);
    lv_obj_align(battery_bar_outer, LV_ALIGN_TOP_RIGHT, -28, 7);

    // Battery terminal (tiny rectangle on the right)
    lv_obj_t *bt = lv_obj_create(battery_bar_outer);
    lv_obj_remove_style_all(bt);
    lv_obj_set_size(bt, 2, 4);
    lv_obj_set_style_bg_color(bt, lv_color_hex(0x888899), 0);
    lv_obj_set_style_bg_opa(bt, LV_OPA_COVER, 0);
    lv_obj_align(bt, LV_ALIGN_RIGHT_MID, 2, 0);
    lv_obj_set_style_radius(bt, 0, 0);

    // Battery fill bar (inside the battery icon)
    lv_obj_t *bf = lv_obj_create(battery_bar_outer);
    lv_obj_remove_style_all(bf);
    lv_obj_set_size(bf, 20, 6);
    lv_obj_set_style_bg_color(bf, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_bg_opa(bf, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bf, 0, 0);
    lv_obj_align(bf, LV_ALIGN_LEFT_MID, 1, 0);
    battery_bar = bf;

    charging_label = lv_label_create(parent);
    lv_label_set_text(charging_label, "");
    lv_obj_align(charging_label, LV_ALIGN_TOP_RIGHT, -56, 5);
    lv_obj_set_style_text_font(charging_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(charging_label, lv_color_hex(0x00d488), 0);

    battery_label = lv_label_create(parent);
    lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -16, 6);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888899), 0);
    lv_label_set_text(battery_label, "--");

    page_dots = lv_label_create(parent);
    lv_label_set_text(page_dots, "\xe2\x97\x8f \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b \xe2\x97\x8b");
    lv_obj_align(page_dots, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_font(page_dots, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(page_dots, lv_color_hex(0x555566), 0);
}

// 鈹€鈹€ Digital watchface 鈹€鈹€
static void watchface_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

    time_label = lv_label_create(parent);
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(time_label);
    lv_obj_set_y(time_label, lv_obj_get_y(time_label) - 24);

    date_label = lv_label_create(parent);
    lv_label_set_text(date_label, "---");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0x888899), 0);
    lv_obj_align_to(date_label, time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    step_label = lv_label_create(parent);
    lv_label_set_text(step_label, "Steps: 0");
    lv_obj_set_style_text_font(step_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(step_label, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(step_label, LV_ALIGN_BOTTOM_MID, 0, -32);
}

// 鈹€鈹€ Sensor page 鈹€鈹€
static void sensor_page_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Sensors");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    accel_label = lv_label_create(parent);
    lv_label_set_text(accel_label, "ACC: --");
    lv_obj_set_style_text_font(accel_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(accel_label, lv_color_hex(0xcccccc), 0);
    lv_obj_align(accel_label, LV_ALIGN_LEFT_MID, 16, -20);

    gyro_label = lv_label_create(parent);
    lv_label_set_text(gyro_label, "GYR: --");
    lv_obj_set_style_text_font(gyro_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gyro_label, lv_color_hex(0xcccccc), 0);
    lv_obj_align(gyro_label, LV_ALIGN_LEFT_MID, 16, 10);

    batt_volt_label = lv_label_create(parent);
    lv_label_set_text(batt_volt_label, "BAT: --");
    lv_obj_set_style_text_font(batt_volt_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(batt_volt_label, lv_color_hex(0x00d488), 0);
    lv_obj_align(batt_volt_label, LV_ALIGN_LEFT_MID, 16, 40);

    intensity_label = lv_label_create(parent);
    lv_label_set_text(intensity_label, "INT: --");
    lv_obj_set_style_text_font(intensity_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(intensity_label, lv_color_hex(0xffaa00), 0);
    lv_obj_align(intensity_label, LV_ALIGN_LEFT_MID, 16, 64);

    calories_label = lv_label_create(parent);
    lv_label_set_text(calories_label, "CAL: --");
    lv_obj_set_style_text_font(calories_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(calories_label, lv_color_hex(0xff4466), 0);
    lv_obj_align(calories_label, LV_ALIGN_LEFT_MID, 16, 88);

    sleep_label = lv_label_create(parent);
    lv_label_set_text(sleep_label, "SLP: --");
    lv_obj_set_style_text_font(sleep_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sleep_label, lv_color_hex(0x8866ff), 0);
    lv_obj_align(sleep_label, LV_ALIGN_LEFT_MID, 16, 112);
}

// 鈹€鈹€ Analog watchface 鈹€鈹€
static void analog_create_hand(lv_obj_t **hand, lv_point_t pts[2],
    int len, int width, lv_color_t color) {
    *hand = lv_line_create(analog_face);
    pts[0].x = CX; pts[0].y = CY;
    pts[1].x = CX + len; pts[1].y = CY - len;
    lv_line_set_points(*hand, pts, 2);
    lv_obj_set_style_line_width(*hand, width, 0);
    lv_obj_set_style_line_color(*hand, color, 0);
    lv_obj_set_style_line_rounded(*hand, 1, 0);
}

static void analog_watchface_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);
    analog_face = parent;

    for (int i = 0; i < 12; i++) {
        float a = (float)i * M_PI / 6.0f - M_PI / 2.0f;
        int outer = 95, inner = 82;
        dial_pts[i][0].x = CX + (int)(cosf(a) * inner);
        dial_pts[i][0].y = CY + (int)(sinf(a) * inner);
        dial_pts[i][1].x = CX + (int)(cosf(a) * outer);
        dial_pts[i][1].y = CY + (int)(sinf(a) * outer);
        dial_marks[i] = lv_line_create(parent);
        lv_line_set_points(dial_marks[i], dial_pts[i], 2);
        lv_obj_set_style_line_width(dial_marks[i], i % 3 == 0 ? 3 : 1, 0);
        lv_obj_set_style_line_color(dial_marks[i], lv_color_hex(0x555566), 0);
    }

    analog_create_hand(&hour_hand, hour_pts, 50, 4, lv_color_hex(0xffffff));
    analog_create_hand(&min_hand, min_pts, 72, 3, lv_color_hex(0xcccccc));
    analog_create_hand(&sec_hand, sec_pts, 80, 1, lv_color_hex(0xff4444));
    analog_inited = true;
}

static void update_analog_hand(lv_obj_t *hand, lv_point_t pts[2],
    float angle_deg, int len) {
    float rad = angle_deg * M_PI / 180.0f;
    pts[1].x = CX + (int)(sinf(rad) * len);
    pts[1].y = CY - (int)(cosf(rad) * len);
    lv_line_set_points(hand, pts, 2);
}

// 鈹€鈹€ Notification page 鈹€鈹€
static void notif_page_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

    // Title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Notifications");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Empty state label
    notif_empty_label = lv_label_create(parent);
    lv_label_set_text(notif_empty_label, "No notifications yet");
    lv_obj_set_style_text_font(notif_empty_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(notif_empty_label, lv_color_hex(0x555566), 0);
    lv_obj_center(notif_empty_label);

    // Scrollable list container
    notif_list = lv_obj_create(parent);
    lv_obj_remove_style_all(notif_list);
    lv_obj_set_size(notif_list, LCD_WIDTH - 16, LCD_HEIGHT - 48);
    lv_obj_align(notif_list, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_flex_flow(notif_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(notif_list, 4, 0);
    lv_obj_set_scroll_dir(notif_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(notif_list, LV_SCROLLBAR_MODE_AUTO);
}

// 鈹€鈹€ Music control page 鈹€鈹€
static void music_page_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Music");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Transport controls
    lv_obj_t *btn_prev = lv_btn_create(parent);
    lv_obj_set_size(btn_prev, 48, 48);
    lv_obj_align(btn_prev, LV_ALIGN_CENTER, -60, 0);
    lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_COVER, 0);
    lv_obj_t *lbl_prev = lv_label_create(btn_prev);
    lv_label_set_text(lbl_prev, "|<");
    lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_prev);
    lv_obj_add_event_cb(btn_prev, [](lv_event_t *e) {
        ble_hid_prev_track();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_play = lv_btn_create(parent);
    lv_obj_set_size(btn_play, 56, 56);
    lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_bg_opa(btn_play, LV_OPA_COVER, 0);
    lv_obj_t *lbl_play = lv_label_create(btn_play);
    lv_label_set_text(lbl_play, "||");
    lv_obj_set_style_text_font(lbl_play, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_play, lv_color_hex(0x000000), 0);
    lv_obj_center(lbl_play);
    lv_obj_add_event_cb(btn_play, [](lv_event_t *e) {
        ble_hid_play_pause();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_next = lv_btn_create(parent);
    lv_obj_set_size(btn_next, 48, 48);
    lv_obj_align(btn_next, LV_ALIGN_CENTER, 60, 0);
    lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_COVER, 0);
    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, ">|");
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_next);
    lv_obj_add_event_cb(btn_next, [](lv_event_t *e) {
        ble_hid_next_track();
    }, LV_EVENT_CLICKED, NULL);

    // Volume controls
    lv_obj_t *btn_vol_down = lv_btn_create(parent);
    lv_obj_set_size(btn_vol_down, 50, 40);
    lv_obj_align(btn_vol_down, LV_ALIGN_CENTER, -35, 55);
    lv_obj_set_style_bg_color(btn_vol_down, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(btn_vol_down, LV_OPA_COVER, 0);
    lv_obj_t *lbl_vd = lv_label_create(btn_vol_down);
    lv_label_set_text(lbl_vd, "-");
    lv_obj_set_style_text_font(lbl_vd, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_vd);
    lv_obj_add_event_cb(btn_vol_down, [](lv_event_t *e) {
        ble_hid_volume_down();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_vol_up = lv_btn_create(parent);
    lv_obj_set_size(btn_vol_up, 50, 40);
    lv_obj_align(btn_vol_up, LV_ALIGN_CENTER, 35, 55);
    lv_obj_set_style_bg_color(btn_vol_up, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(btn_vol_up, LV_OPA_COVER, 0);
    lv_obj_t *lbl_vu = lv_label_create(btn_vol_up);
    lv_label_set_text(lbl_vu, "+");
    lv_obj_set_style_text_font(lbl_vu, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_vu);
    lv_obj_add_event_cb(btn_vol_up, [](lv_event_t *e) {
        ble_hid_volume_up();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Connect phone to use");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555566), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -26);
}

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
// Public API
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?

void ui_pages_init(lv_obj_t **pages, int num_pages, CST816S *touch_dev) {
    // Pages: 0=watchface, 1=analog, 2=sensor, 3=notif, 4=stopwatch,
    //        5=weather, 6=activity, 7=player, 8=voice, 9=music
    pages[0] = lv_obj_create(NULL); watchface_create(pages[0]);
    pages[1] = lv_obj_create(NULL); analog_watchface_create(pages[1]);
    pages[2] = lv_obj_create(NULL); sensor_page_create(pages[2]);
    pages[3] = lv_obj_create(NULL); notif_page_create(pages[3]);
    pages[4] = lv_obj_create(NULL); stopwatch_create(pages[4]);
    pages[5] = lv_obj_create(NULL); weather_create(pages[5]);
    pages[6] = lv_obj_create(NULL); activity_create(pages[6]);
    pages[7] = lv_obj_create(NULL); player_create(pages[7]);
    pages[8] = lv_obj_create(NULL); voice_chat_page_create(pages[8]);
    pages[9] = lv_obj_create(NULL); music_page_create(pages[9]);
    status_bar_create(lv_layer_top());
    lv_scr_load(pages[0]);
}

int ui_get_num_pages(void) { return PAGE_COUNT; }

void ui_set_step_label(int steps) {
    if (step_label)
        lv_label_set_text_fmt(step_label, "Steps: %d", steps);
}

void ui_update_watchface(const ui_telemetry_t *t) {
    snprintf(time_str, sizeof(time_str), "%02d:%02d", t->hour, t->minute);
    lv_label_set_text(time_label, time_str);

    static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(date_str, sizeof(date_str), "%s %s %d",
        wd[t->week < 7 ? t->week : 0],
        mo[t->month >= 1 && t->month <= 12 ? t->month - 1 : 0], t->day);
    lv_label_set_text(date_label, date_str);

    int batt = t->batt_percent;
    if (t->batt_valid && batt >= 0 && batt <= 100 && batt != last_batt) {
        last_batt = batt;
        lv_label_set_text_fmt(battery_label, "%d%%", batt);
        lv_obj_set_style_text_color(battery_label,
            batt < 20 ? lv_color_hex(0xff3333) : lv_color_hex(0x888899), 0);
        int bw = (batt * 20) / 100;
        if (bw < 2 && batt > 0) bw = 2;
        lv_obj_set_width(battery_bar, bw);
        lv_obj_set_style_bg_color(battery_bar,
            batt < 20 ? lv_color_hex(0xff3333) :
            batt < 50 ? lv_color_hex(0xffaa00) : lv_color_hex(0x00d4ff), 0);
        ble_srv_update_battery(batt);
    } else if (!t->batt_valid && last_batt != -2) {
        last_batt = -2;
        lv_label_set_text(battery_label, "USB");
        lv_obj_set_style_text_color(battery_label, lv_color_hex(0x00d488), 0);
        lv_obj_set_width(battery_bar, 20);
        lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x00d488), 0);
    }

    if (charging_label) {
        lv_label_set_text(charging_label, t->charging ? "+" : "");
        lv_obj_set_style_text_color(charging_label,
            t->charging ? lv_color_hex(0x00d488) : lv_color_hex(0x111122), 0);
    }

    if (wifi_icon) {
        lv_obj_set_style_text_color(wifi_icon,
            t->wifi_connected ? lv_color_hex(0x00d4ff) : lv_color_hex(0x333344), 0);
    }

    if (step_label)
        lv_label_set_text_fmt(step_label, "Steps: %d", t->step_count);
}

void ui_update_analog(int hour, int minute, int second) {
    if (!analog_inited) return;
    int h = hour % 12, m = minute, s = second;
    if (h == prev_hour && m == prev_min && s == prev_sec) return;
    prev_hour = h; prev_min = m; prev_sec = s;
    update_analog_hand(hour_hand, hour_pts, h * 30 + m * 0.5f, 50);
    update_analog_hand(min_hand, min_pts, m * 6, 72);
    update_analog_hand(sec_hand, sec_pts, s * 6, 80);
}

void ui_update_sensor_page(const ui_telemetry_t *t) {
    lv_label_set_text_fmt(accel_label, "ACC %+03d %+03d %+03d",
        (int)(t->acc_x * 100), (int)(t->acc_y * 100), (int)(t->acc_z * 100));
    lv_label_set_text_fmt(gyro_label, "GYR %+04d %+04d %+04d",
        (int)(t->gyr_x * 10), (int)(t->gyr_y * 10), (int)(t->gyr_z * 10));
    if (t->batt_valid) {
        lv_label_set_text_fmt(batt_volt_label, "BAT %dmV %d%%",
            t->batt_mv, t->batt_percent);
    } else {
        lv_label_set_text(batt_volt_label, "BAT USB");
    }
    lv_label_set_text_fmt(intensity_label, "INT %d%% METs:%.1f",
        t->intensity, t->mets);
    lv_label_set_text_fmt(calories_label, "CAL %.0fkcal",
        t->calories);
    if (t->sleeping) {
        lv_label_set_text_fmt(sleep_label, "SLP %dh%dm D:%dm",
            t->sleep_total_min / 60, t->sleep_total_min % 60, t->sleep_deep_min);
    } else if (t->sleep_total_min > 0) {
        lv_label_set_text_fmt(sleep_label, "SLP %dh%dm (awake)",
            t->sleep_total_min / 60, t->sleep_total_min % 60);
    } else {
        lv_label_set_text(sleep_label, "SLP: --");
    }
}

void ui_update_notif_page(void) {
    int cnt = notif_history_count();

    // Only rebuild when count changes (new notification added or cleared)
    if (cnt == last_notif_count) return;
    last_notif_count = cnt;

    // Clear existing list items
    lv_obj_clean(notif_list);

    if (cnt == 0) {
        lv_obj_clear_flag(notif_empty_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(notif_empty_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < cnt; i++) {
        const notif_entry_t *e = notif_history_get(i);
        if (!e) break;

        // Create a card-like container for each notification
        lv_obj_t *card = lv_obj_create(notif_list);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 8, 0);

        // Time + App header
        char hdr[40];
        snprintf(hdr, sizeof(hdr), "%02d:%02d  %s", e->hour, e->minute, e->app);
        lv_obj_t *h = lv_label_create(card);
        lv_label_set_text(h, hdr);
        lv_obj_set_width(h, LV_PCT(100));
        lv_obj_set_style_text_font(h, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x555566), 0);

        // Title
        lv_obj_t *t = lv_label_create(card);
        lv_label_set_text(t, e->title);
        lv_obj_set_width(t, LV_PCT(100));
        lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xffffff), 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);

        // Body (truncated)
        if (e->body[0]) {
            lv_obj_t *b = lv_label_create(card);
            lv_label_set_text(b, e->body);
            lv_obj_set_width(b, LV_PCT(100));
            lv_obj_set_style_text_font(b, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(b, lv_color_hex(0x888899), 0);
            lv_label_set_long_mode(b, LV_LABEL_LONG_DOT);
        }
    }
}
