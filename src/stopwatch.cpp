#include "stopwatch.h"
#include <Arduino.h>
#include <lvgl.h>

typedef enum { MODE_STOPWATCH, MODE_TIMER } StopwatchMode;

static StopwatchMode mode = MODE_STOPWATCH;
static bool running = false;
static unsigned long start_ms = 0;
static unsigned long elapsed_ms = 0;
static unsigned long pause_offset = 0;
static unsigned long timer_target_ms = 10000;

static lv_obj_t *time_label = nullptr;
static lv_obj_t *mode_label = nullptr;
static lv_obj_t *action_btn = nullptr;
static lv_obj_t *action_btn_label = nullptr;
static lv_obj_t *reset_btn = nullptr;
static lv_obj_t *mode_btn = nullptr;
static lv_obj_t *timer_add_btn = nullptr;
static lv_obj_t *status_label = nullptr;

static void update_display(void) {
  unsigned long current;
  if (running) {
    current = elapsed_ms + (millis() - pause_offset);
  } else {
    current = elapsed_ms;
  }

  if (mode == MODE_TIMER && current >= timer_target_ms) {
    if (running) {
      running = false;
      elapsed_ms = timer_target_ms;
    }
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xff3333), 0);
    lv_label_set_text(status_label, "TIME'S UP!");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xff3333), 0);
    return;
  }

  lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
  unsigned long remain;
  if (mode == MODE_TIMER) {
    remain = (current > timer_target_ms) ? 0 : timer_target_ms - current;
  } else {
    remain = current;
  }

  unsigned long cs = (remain / 10) % 100;
  unsigned long sec = (remain / 1000) % 60;
  unsigned long min = (remain / 60000) % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu.%02lu", min, sec, cs);
  lv_label_set_text(time_label, buf);

  if (mode == MODE_TIMER) {
    lv_label_set_text_fmt(status_label, "Set: %lus", timer_target_ms / 1000);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888899), 0);
  } else {
    lv_label_set_text(status_label, running ? "Running" : "Stopped");
    lv_obj_set_style_text_color(status_label,
      running ? lv_color_hex(0x00d488) : lv_color_hex(0x888899), 0);
  }
}

static void on_action_click(lv_event_t *e) {
  (void)e;
  if (running) {
    // Stop
    running = false;
    elapsed_ms += (millis() - pause_offset);
    lv_label_set_text(action_btn_label, "Start");
  } else {
    // Start
    running = true;
    pause_offset = millis();
    lv_label_set_text(action_btn_label, "Stop");
  }
}

static void on_reset_click(lv_event_t *e) {
  (void)e;
  running = false;
  elapsed_ms = 0;
  pause_offset = 0;
  lv_label_set_text(action_btn_label, "Start");
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
  update_display();
}

static void on_mode_click(lv_event_t *e) {
  (void)e;
  running = false;
  elapsed_ms = 0;
  pause_offset = 0;
  mode = (mode == MODE_STOPWATCH) ? MODE_TIMER : MODE_STOPWATCH;
  lv_label_set_text(action_btn_label, "Start");
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);

  if (mode == MODE_TIMER) {
    lv_label_set_text(mode_label, "TIMER");
    lv_obj_clear_flag(timer_add_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(mode_label, "STOPWATCH");
    lv_obj_add_flag(timer_add_btn, LV_OBJ_FLAG_HIDDEN);
  }
  update_display();
}

static void on_timer_add_click(lv_event_t *e) {
  (void)e;
  timer_target_ms += 10000;
  if (timer_target_ms > 5999000) timer_target_ms = 10000; // wrap at ~99min
  update_display();
}

void stopwatch_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  mode_label = lv_label_create(parent);
  lv_label_set_text(mode_label, "STOPWATCH");
  lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(mode_label, lv_color_hex(0x555566), 0);
  lv_obj_align(mode_label, LV_ALIGN_TOP_MID, 0, 8);

  time_label = lv_label_create(parent);
  lv_label_set_text(time_label, "00:00.00");
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
  lv_obj_center(time_label);
  lv_obj_set_y(time_label, lv_obj_get_y(time_label) - 20);

  status_label = lv_label_create(parent);
  lv_label_set_text(status_label, "Stopped");
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x888899), 0);
  lv_obj_align_to(status_label, time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  // Action button (Start/Stop)
  action_btn = lv_btn_create(parent);
  lv_obj_set_size(action_btn, 70, 44);
  lv_obj_align(action_btn, LV_ALIGN_BOTTOM_MID, -76, -50);
  lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x00d4ff), 0);
  lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x0099bb), LV_STATE_PRESSED);
  action_btn_label = lv_label_create(action_btn);
  lv_label_set_text(action_btn_label, "Start");
  lv_obj_center(action_btn_label);
  lv_obj_add_event_cb(action_btn, on_action_click, LV_EVENT_CLICKED, NULL);

  // Reset button
  reset_btn = lv_btn_create(parent);
  lv_obj_set_size(reset_btn, 70, 44);
  lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x444466), 0);
  lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x333355), LV_STATE_PRESSED);
  lv_obj_t *reset_lbl = lv_label_create(reset_btn);
  lv_label_set_text(reset_lbl, "Reset");
  lv_obj_center(reset_lbl);
  lv_obj_add_event_cb(reset_btn, on_reset_click, LV_EVENT_CLICKED, NULL);

  // Mode switch button
  mode_btn = lv_btn_create(parent);
  lv_obj_set_size(mode_btn, 70, 44);
  lv_obj_align(mode_btn, LV_ALIGN_BOTTOM_MID, 76, -50);
  lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x555566), 0);
  lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x444477), LV_STATE_PRESSED);
  lv_obj_t *mode_lbl = lv_label_create(mode_btn);
  lv_label_set_text(mode_lbl, "Mode");
  lv_obj_center(mode_lbl);
  lv_obj_add_event_cb(mode_btn, on_mode_click, LV_EVENT_CLICKED, NULL);

  // Timer +10s button (hidden by default)
  timer_add_btn = lv_btn_create(parent);
  lv_obj_set_size(timer_add_btn, 100, 36);
  lv_obj_align(timer_add_btn, LV_ALIGN_BOTTOM_MID, 0, -100);
  lv_obj_set_style_bg_color(timer_add_btn, lv_color_hex(0x664433), 0);
  lv_obj_set_style_bg_color(timer_add_btn, lv_color_hex(0x553322), LV_STATE_PRESSED);
  lv_obj_t *add_lbl = lv_label_create(timer_add_btn);
  lv_label_set_text(add_lbl, "+10s");
  lv_obj_center(add_lbl);
  lv_obj_add_event_cb(timer_add_btn, on_timer_add_click, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(timer_add_btn, LV_OBJ_FLAG_HIDDEN);
}

void stopwatch_update(void) {
  if (running) update_display();
}
