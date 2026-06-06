// Settings page: step goal, brightness, DND, about
#include "settings_page.h"
#include "nvs_store.h"
#include "service/ota_update.h"
#include "service/ble_srv.h"
#include "pin_config.h"
#include "debug_log.h"

static lv_obj_t *goal_label = nullptr;
static lv_obj_t *bright_label = nullptr;
static lv_obj_t *dnd_label = nullptr;
static lv_obj_t *version_label = nullptr;

static int step_goal = 8000;
static int brightness = 100;
static bool dnd_on = false;

static void on_goal_minus(lv_event_t *e) {
    step_goal -= 500;
    if (step_goal < 1000) step_goal = 1000;
    nvs_set_step_goal(step_goal);
    lv_label_set_text_fmt(goal_label, "Goal: %d", step_goal);
    LOG_INFO("Settings: step goal=%d", step_goal);
}

static void on_goal_plus(lv_event_t *e) {
    step_goal += 500;
    if (step_goal > 30000) step_goal = 30000;
    nvs_set_step_goal(step_goal);
    lv_label_set_text_fmt(goal_label, "Goal: %d", step_goal);
}

static void on_bright_minus(lv_event_t *e) {
    brightness -= 10;
    if (brightness < 10) brightness = 10;
    nvs_set_brightness(brightness);
    lv_label_set_text_fmt(bright_label, "Bright: %d%%", brightness);
    // Apply brightness via PWM
    // Note: simple on/off for now; full PWM would use ledcWrite
    LOG_INFO("Settings: brightness=%d%%", brightness);
}

static void on_bright_plus(lv_event_t *e) {
    brightness += 10;
    if (brightness > 100) brightness = 100;
    nvs_set_brightness(brightness);
    lv_label_set_text_fmt(bright_label, "Bright: %d%%", brightness);
    LOG_INFO("Settings: brightness=%d%%", brightness);
}

static void on_dnd_toggle(lv_event_t *e) {
    dnd_on = !dnd_on;
    nvs_set_dnd(dnd_on);
    ble_srv_set_dnd(dnd_on);
    lv_label_set_text(dnd_label, dnd_on ? "DND: ON" : "DND: OFF");
    lv_obj_set_style_text_color(dnd_label,
        dnd_on ? lv_color_hex(0xffaa00) : lv_color_hex(0x888899), 0);
    LOG_INFO("Settings: DND=%s", dnd_on ? "ON" : "OFF");
}

// Helper: create a row with label and +/- buttons
static lv_obj_t* create_setting_row(lv_obj_t *parent, const char *title,
    lv_obj_t **value_label, int y_pos,
    lv_event_cb_t on_minus, lv_event_cb_t on_plus)
{
    // Label
    *value_label = lv_label_create(parent);
    lv_label_set_text(*value_label, title);
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(*value_label, LV_ALIGN_LEFT_MID, 16, y_pos);

    if (on_minus || on_plus) {
        // Minus button
        lv_obj_t *btn_m = lv_btn_create(parent);
        lv_obj_set_size(btn_m, 40, 36);
        lv_obj_align(btn_m, LV_ALIGN_RIGHT_MID, -82, y_pos);
        lv_obj_set_style_bg_color(btn_m, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(btn_m, LV_OPA_COVER, 0);
        lv_obj_t *lm = lv_label_create(btn_m);
        lv_label_set_text(lm, "-");
        lv_obj_set_style_text_font(lm, &lv_font_montserrat_16, 0);
        lv_obj_center(lm);
        if (on_minus) lv_obj_add_event_cb(btn_m, on_minus, LV_EVENT_CLICKED, NULL);

        // Plus button
        lv_obj_t *btn_p = lv_btn_create(parent);
        lv_obj_set_size(btn_p, 40, 36);
        lv_obj_align(btn_p, LV_ALIGN_RIGHT_MID, -36, y_pos);
        lv_obj_set_style_bg_color(btn_p, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(btn_p, LV_OPA_COVER, 0);
        lv_obj_t *lp = lv_label_create(btn_p);
        lv_label_set_text(lp, "+");
        lv_obj_set_style_text_font(lp, &lv_font_montserrat_16, 0);
        lv_obj_center(lp);
        if (on_plus) lv_obj_add_event_cb(btn_p, on_plus, LV_EVENT_CLICKED, NULL);
    }

    return *value_label;
}

lv_obj_t* settings_page_create(void) {
    lv_obj_t *page = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x0d0d1a), 0);

    // Title
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Load saved settings
    step_goal = nvs_get_step_goal();
    brightness = nvs_get_brightness();
    dnd_on = nvs_get_dnd();

    // Step goal row (y=40)
    create_setting_row(page, "", &goal_label, 0, on_goal_minus, on_goal_plus);
    lv_label_set_text_fmt(goal_label, "Goal: %d", step_goal);
    lv_obj_align(goal_label, LV_ALIGN_LEFT_MID, 16, -40);
    // Re-align buttons relative to goal_label
    lv_obj_t *btn_gm = lv_obj_get_child(page, 2);  // minus btn
    lv_obj_t *btn_gp = lv_obj_get_child(page, 3);  // plus btn
    lv_obj_align(btn_gm, LV_ALIGN_LEFT_MID, 136, -40);
    lv_obj_align(btn_gp, LV_ALIGN_LEFT_MID, 184, -40);

    // Brightness row (y=80)
    create_setting_row(page, "", &bright_label, 0, on_bright_minus, on_bright_plus);
    lv_label_set_text_fmt(bright_label, "Bright: %d%%", brightness);
    lv_obj_align(bright_label, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t *btn_bm = lv_obj_get_child(page, 5);
    lv_obj_t *btn_bp = lv_obj_get_child(page, 6);
    lv_obj_align(btn_bm, LV_ALIGN_LEFT_MID, 136, 0);
    lv_obj_align(btn_bp, LV_ALIGN_LEFT_MID, 184, 0);

    // DND toggle row (y=120)
    dnd_label = lv_label_create(page);
    lv_label_set_text(dnd_label, dnd_on ? "DND: ON" : "DND: OFF");
    lv_obj_set_style_text_font(dnd_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dnd_label,
        dnd_on ? lv_color_hex(0xffaa00) : lv_color_hex(0x888899), 0);
    lv_obj_align(dnd_label, LV_ALIGN_LEFT_MID, 16, 40);

    lv_obj_t *btn_dnd = lv_btn_create(page);
    lv_obj_set_size(btn_dnd, 64, 36);
    lv_obj_align(btn_dnd, LV_ALIGN_LEFT_MID, 136, 40);
    lv_obj_set_style_bg_color(btn_dnd, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(btn_dnd, LV_OPA_COVER, 0);
    lv_obj_t *lbl_dnd = lv_label_create(btn_dnd);
    lv_label_set_text(lbl_dnd, "Toggle");
    lv_obj_set_style_text_font(lbl_dnd, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_dnd);
    lv_obj_add_event_cb(btn_dnd, on_dnd_toggle, LV_EVENT_CLICKED, NULL);

    // About section
    version_label = lv_label_create(page);
    lv_label_set_text_fmt(version_label, "FW: %s", FIRMWARE_VERSION);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(version_label, lv_color_hex(0x555566), 0);
    lv_obj_align(version_label, LV_ALIGN_BOTTOM_MID, 0, -24);

    return page;
}

void settings_page_update(void) {
    // Refresh displayed values in case settings changed externally
    if (goal_label)
        lv_label_set_text_fmt(goal_label, "Goal: %d", nvs_get_step_goal());
    if (bright_label)
        lv_label_set_text_fmt(bright_label, "Bright: %d%%", nvs_get_brightness());
    if (version_label)
        lv_label_set_text_fmt(version_label, "FW: %s", FIRMWARE_VERSION);
}
