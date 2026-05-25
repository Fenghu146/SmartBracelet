#include "activity.h"
#include "activity_model.h"
#include <Arduino.h>
#include <math.h>
#include <lvgl.h>

#define WINDOW 50
#define STRIDE 25

static float buf[WINDOW][6];
static int head = 0;
static int count = 0;

static lv_obj_t *activity_label = nullptr;
static lv_obj_t *confidence_label = nullptr;
static lv_obj_t *feature_label = nullptr;

static const char *class_names[3] = {"WALK", "RUN", "IDLE"};
static int last_pred = -1;
static int stable_pred = -1;
static int stable_count = 0;

int activity_get_current(void) { return stable_pred; }

void activity_push_data(float ax, float ay, float az,
                        float gx, float gy, float gz) {
  buf[head][0] = ax;
  buf[head][1] = ay;
  buf[head][2] = az;
  buf[head][3] = gx;
  buf[head][4] = gy;
  buf[head][5] = gz;
  head = (head + 1) % WINDOW;
  if (count < WINDOW) count++;
}

static int activity_predict(void) {
  if (count < WINDOW) return -1;

  // Build window in chronological order
  float win[WINDOW][6];
  for (int i = 0; i < WINDOW; i++) {
    int idx = (head + i) % WINDOW;
    for (int j = 0; j < 6; j++) win[i][j] = buf[idx][j];
  }

  // 12 features: mean(6) + std(6)
  float features[12];
  for (int j = 0; j < 6; j++) {
    float sum = 0, sum2 = 0;
    for (int i = 0; i < WINDOW; i++) {
      sum += win[i][j];
      sum2 += win[i][j] * win[i][j];
    }
    float m = sum / WINDOW;
    features[j] = m;       // mean
    features[11 - j] = sqrtf(sum2 / WINDOW - m * m);  // std
  }

  if (feature_label) {
    lv_label_set_text_fmt(feature_label,
      "ax:%.2f ay:%.2f\nax_std:%.3f ay_std:%.3f",
      features[0], features[1], features[6], features[7]);
  }

  return rf_predict(features);
}

void activity_create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d0d1a), 0);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Activity AI");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  activity_label = lv_label_create(parent);
  lv_label_set_text(activity_label, "---");
  lv_obj_set_style_text_font(activity_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(activity_label, lv_color_hex(0xffffff), 0);
  lv_obj_center(activity_label);
  lv_obj_set_y(activity_label, lv_obj_get_y(activity_label) - 20);

  confidence_label = lv_label_create(parent);
  lv_label_set_text(confidence_label, "Collecting data...");
  lv_obj_set_style_text_font(confidence_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(confidence_label, lv_color_hex(0x888899), 0);
  lv_obj_align_to(confidence_label, activity_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  feature_label = lv_label_create(parent);
  lv_label_set_text(feature_label, "");
  lv_obj_set_style_text_font(feature_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(feature_label, lv_color_hex(0x555566), 0);
  lv_obj_align(feature_label, LV_ALIGN_BOTTOM_LEFT, 8, -12);
}

void activity_update(void) {
  int pred = activity_predict();
  if (pred < 0) {
    lv_label_set_text(confidence_label, "Collecting data...");
    return;
  }

  // Stabilize: require same prediction 3 times in a row
  if (pred == last_pred) {
    stable_count++;
    if (stable_count >= 3 && pred != stable_pred) {
      stable_pred = pred;
      lv_label_set_text(activity_label, class_names[pred]);
      // Color by activity
      uint32_t colors[3] = {0x00d4ff, 0xff4444, 0x888899};
      lv_obj_set_style_text_color(activity_label, lv_color_hex(colors[pred]), 0);
      lv_label_set_text(confidence_label, "Live");
    }
  } else {
    stable_count = 0;
  }
  last_pred = pred;
}
