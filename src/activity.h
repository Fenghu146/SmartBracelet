#pragma once
#include <lvgl.h>

void activity_create(lv_obj_t *parent);
void activity_update(void);
void activity_push_data(float ax, float ay, float az,
                        float gx, float gy, float gz);
int  activity_get_current(void);  // -1 unknown, 0 walk, 1 run, 2 idle

// Get the latest computed features for BLE upload / AI co-inference
// Returns pointer to static array of 12 floats (mean[6] + std[6]), or NULL if no data yet
const float* activity_get_features(void);
