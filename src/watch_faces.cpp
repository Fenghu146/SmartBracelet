// Sport watch face: time + step ring + calories + activity
#include "watch_faces.h"
#include "pin_config.h"
#include <math.h>
#include <stdio.h>

#ifndef LCD_WIDTH
#define LCD_WIDTH 240
#define LCD_HEIGHT 284
#endif

static const int CX = 120, CY = 142;

static lv_obj_t *sf_time_label = nullptr;
static lv_obj_t *sf_date_label = nullptr;
static lv_obj_t *sf_step_label = nullptr;
static lv_obj_t *sf_cal_label = nullptr;
static lv_obj_t *sf_act_label = nullptr;
static lv_obj_t *sf_step_arc = nullptr;
static lv_obj_t *sf_step_arc_bg = nullptr;

static const char *sf_face_names[FACE_COUNT] = {"Digital", "Analog", "Sport"};

void sport_face_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

    // Step progress ring (background)
    sf_step_arc_bg = lv_arc_create(parent);
    lv_arc_set_bg_angles(sf_step_arc_bg, 0, 360);
    lv_arc_set_range(sf_step_arc_bg, 0, 100);
    lv_arc_set_value(sf_step_arc_bg, 0);
    lv_obj_set_size(sf_step_arc_bg, 200, 200);
    lv_obj_center(sf_step_arc_bg);
    lv_obj_set_style_arc_color(sf_step_arc_bg, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_arc_width(sf_step_arc_bg, 8, LV_PART_MAIN);
    lv_obj_remove_style(sf_step_arc_bg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(sf_step_arc_bg, LV_OBJ_FLAG_CLICKABLE);

    // Step progress ring (foreground)
    sf_step_arc = lv_arc_create(parent);
    lv_arc_set_bg_angles(sf_step_arc, 0, 360);
    lv_arc_set_range(sf_step_arc, 0, 100);
    lv_arc_set_value(sf_step_arc, 0);
    lv_obj_set_size(sf_step_arc, 200, 200);
    lv_obj_center(sf_step_arc);
    lv_obj_set_style_arc_color(sf_step_arc, lv_color_hex(0x00d4ff), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sf_step_arc, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(sf_step_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(sf_step_arc, LV_OBJ_FLAG_CLICKABLE);

    // Time (large, center-top area)
    sf_time_label = lv_label_create(parent);
    lv_label_set_text(sf_time_label, "00:00");
    lv_obj_set_style_text_font(sf_time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(sf_time_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(sf_time_label);
    lv_obj_set_y(sf_time_label, -30);

    // Date
    sf_date_label = lv_label_create(parent);
    lv_label_set_text(sf_date_label, "---");
    lv_obj_set_style_text_font(sf_date_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sf_date_label, lv_color_hex(0x888899), 0);
    lv_obj_align_to(sf_date_label, sf_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    // Steps (center of ring)
    sf_step_label = lv_label_create(parent);
    lv_label_set_text(sf_step_label, "0");
    lv_obj_set_style_text_font(sf_step_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sf_step_label, lv_color_hex(0x00d4ff), 0);
    lv_obj_center(sf_step_label);
    lv_obj_set_y(sf_step_label, 10);

    // Calories (bottom-left)
    sf_cal_label = lv_label_create(parent);
    lv_label_set_text(sf_cal_label, "0 kcal");
    lv_obj_set_style_text_font(sf_cal_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sf_cal_label, lv_color_hex(0xff4466), 0);
    lv_obj_align(sf_cal_label, LV_ALIGN_BOTTOM_LEFT, 16, -30);

    // Activity (bottom-right)
    sf_act_label = lv_label_create(parent);
    lv_label_set_text(sf_act_label, "---");
    lv_obj_set_style_text_font(sf_act_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sf_act_label, lv_color_hex(0x00d488), 0);
    lv_obj_align(sf_act_label, LV_ALIGN_BOTTOM_RIGHT, -16, -30);
}

void sport_face_update(const ui_telemetry_t *t) {
    if (!sf_time_label) return;

    static char time_str[12], date_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", t->hour, t->minute);
    lv_label_set_text(sf_time_label, time_str);

    static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(date_str, sizeof(date_str), "%s %s %d",
        wd[t->week < 7 ? t->week : 0],
        mo[t->month >= 1 && t->month <= 12 ? t->month - 1 : 0], t->day);
    lv_label_set_text(sf_date_label, date_str);

    // Steps + ring
    char step_str[16];
    snprintf(step_str, sizeof(step_str), "%d", t->step_count);
    lv_label_set_text(sf_step_label, step_str);

    // Step goal assumed 8000 (could be read from NVS)
    int pct = (t->step_count * 100) / 8000;
    if (pct > 100) pct = 100;
    lv_arc_set_value(sf_step_arc, pct);

    // Color the ring based on progress
    uint32_t arc_color = (pct >= 100) ? 0x00d488 : (pct >= 50) ? 0x00d4ff : 0xffaa00;
    lv_obj_set_style_arc_color(sf_step_arc, lv_color_hex(arc_color), LV_PART_INDICATOR);

    // Calories
    char cal_str[20];
    snprintf(cal_str, sizeof(cal_str), "%.0f kcal", t->calories);
    lv_label_set_text(sf_cal_label, cal_str);

    // Activity
    static const char *act_names[] = {"Walk", "Run", "Idle"};
    int act = (t->intensity > 30) ? 0 : (t->intensity > 60) ? 1 : 2;
    lv_label_set_text(sf_act_label, act_names[act]);
}

int watch_face_next(int current_face) {
    return (current_face + 1) % FACE_COUNT;
}

const char* watch_face_name(int face) {
    if (face >= 0 && face < FACE_COUNT) return sf_face_names[face];
    return "Unknown";
}
