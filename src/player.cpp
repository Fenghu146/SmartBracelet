#include "player.h"
#include "service/tf_card.h"
#include "service/audio.h"
#include <Arduino.h>
#include <lvgl.h>

#define MAX_FILES 20

static lv_obj_t *status_label = nullptr;
static lv_obj_t *file_label = nullptr;
static lv_obj_t *tf_label = nullptr;
static lv_obj_t *play_btn = nullptr;
static lv_obj_t *play_btn_lbl = nullptr;

static char file_list[MAX_FILES][32];
static int file_count = 0;
static int selected = -1;
static bool paused = false;

static void refresh_file_list(void) {
  file_count = tf_list_dir("/", file_list, MAX_FILES);
  // Filter to only .wav
  int j = 0;
  for (int i = 0; i < file_count; i++) {
    const char *ext = strrchr(file_list[i], '.');
    if (ext && (strcasecmp(ext, ".wav") == 0)) {
      if (i != j) strcpy(file_list[j], file_list[i]);
      j++;
    }
  }
  file_count = j;
  if (file_count > 0 && selected < 0) selected = 0;
}

static void update_file_display(void) {
  if (file_count == 0) {
    lv_label_set_text(file_label, "No .wav files");
    return;
  }
  if (selected >= 0 && selected < file_count) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%d/%d %s", selected + 1, file_count, file_list[selected]);
    lv_label_set_text(file_label, buf);
  }
}

static void on_play_click(lv_event_t *e) {
  (void)e;
  if (audio_is_playing()) {
    audio_stop();
    paused = false;
    lv_label_set_text(play_btn_lbl, "Play");
    lv_label_set_text(status_label, "Stopped");
    return;
  }
  if (file_count == 0 || selected < 0) {
    lv_label_set_text(status_label, "No file selected");
    return;
  }
  char path[64];
  snprintf(path, sizeof(path), "/%s", file_list[selected]);
  if (audio_play_wav(path)) {
    lv_label_set_text(play_btn_lbl, "Stop");
    lv_label_set_text(status_label, "Playing");
  } else {
    lv_label_set_text(status_label, "Play failed");
  }
}

static void on_next_click(lv_event_t *e) {
  (void)e;
  if (file_count == 0) return;
  selected = (selected + 1) % file_count;
  update_file_display();
  if (audio_is_playing()) {
    audio_stop();
    lv_label_set_text(play_btn_lbl, "Play");
    lv_label_set_text(status_label, "Stopped");
  }
}

void player_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Audio Player");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // TF card info
  tf_label = lv_label_create(parent);
  lv_obj_set_style_text_font(tf_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(tf_label, lv_color_hex(0x555566), 0);
  lv_obj_align(tf_label, LV_ALIGN_TOP_LEFT, 16, 32);

  // Selected file
  file_label = lv_label_create(parent);
  lv_label_set_text(file_label, "Scanning...");
  lv_obj_set_style_text_font(file_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(file_label, lv_color_hex(0xffffff), 0);
  lv_obj_align(file_label, LV_ALIGN_LEFT_MID, 16, -20);
  lv_obj_set_width(file_label, 208);

  // Status
  status_label = lv_label_create(parent);
  lv_label_set_text(status_label, "Ready");
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x888899), 0);
  lv_obj_align_to(status_label, file_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  // Play/Stop button
  play_btn = lv_btn_create(parent);
  lv_obj_set_size(play_btn, 84, 48);
  lv_obj_align(play_btn, LV_ALIGN_BOTTOM_MID, -56, -50);
  lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x00d4ff), 0);
  lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x0099bb), LV_STATE_PRESSED);
  play_btn_lbl = lv_label_create(play_btn);
  lv_label_set_text(play_btn_lbl, "Play");
  lv_obj_center(play_btn_lbl);
  lv_obj_add_event_cb(play_btn, on_play_click, LV_EVENT_CLICKED, NULL);

  // Next button
  lv_obj_t *next_btn = lv_btn_create(parent);
  lv_obj_set_size(next_btn, 64, 48);
  lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 36, -50);
  lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x444466), 0);
  lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x333355), LV_STATE_PRESSED);
  lv_obj_t *next_lbl = lv_label_create(next_btn);
  lv_label_set_text(next_lbl, "Next");
  lv_obj_center(next_lbl);
  lv_obj_add_event_cb(next_btn, on_next_click, LV_EVENT_CLICKED, NULL);

  // Initial scan
  refresh_file_list();
  update_file_display();

  if (tf_available()) {
    uint64_t total = tf_total_kb();
    uint64_t used = tf_used_kb();
    char buf[48];
    snprintf(buf, sizeof(buf), "TF: %lluMB / %lluMB  %d files",
      used / 1024, total / 1024, file_count);
    lv_label_set_text(tf_label, buf);
  } else {
    lv_label_set_text(tf_label, "No TF card");
  }
}

void player_update(void) {
  if (audio_is_playing()) {
    lv_label_set_text(status_label, "Playing...");
    lv_label_set_text(play_btn_lbl, "Stop");
  }
}
