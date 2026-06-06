// Motion intensity estimation from IMU accelerometer data
// Computes activity intensity (0-100) and METs approximation
// Uses variance of acceleration magnitude over sliding window
#include "motion_intensity.h"
#include <Arduino.h>
#include <math.h>

#define WINDOW_SIZE 64  // ~0.5s at 125Hz

static float mag_buf[WINDOW_SIZE];
static int mag_head = 0;
static int mag_count = 0;
static float filtered_intensity = 0.0f;  // smoothed 0-100
static float calories = 0.0f;  // accumulated since reset
static unsigned long last_update_ms = 0;

// Default body weight for calorie estimation (kg)
#define DEFAULT_WEIGHT_KG 65.0f

void motion_intensity_init(void) {
    for (int i = 0; i < WINDOW_SIZE; i++) mag_buf[i] = 1.0f;
    mag_head = 0;
    mag_count = 0;
    filtered_intensity = 0.0f;
    calories = 0.0f;
    last_update_ms = 0;
}

void motion_intensity_update(float ax, float ay, float az) {
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    mag_buf[mag_head] = mag;
    mag_head = (mag_head + 1) % WINDOW_SIZE;
    if (mag_count < WINDOW_SIZE) mag_count++;

    if (mag_count < WINDOW_SIZE) return;

    // Compute variance of magnitude over window
    float sum = 0, sum2 = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        sum += mag_buf[i];
        sum2 += mag_buf[i] * mag_buf[i];
    }
    float mean = sum / WINDOW_SIZE;
    float variance = sum2 / WINDOW_SIZE - mean * mean;
    if (variance < 0) variance = 0;

    // Map variance to intensity 0-100
    // Typical values: idle ~0.001, walk ~0.05, run ~0.2, vigorous ~0.5+
    float intensity = variance * 200.0f;  // scale factor
    if (intensity > 100.0f) intensity = 100.0f;

    // Low-pass filter for smooth display
    filtered_intensity = 0.9f * filtered_intensity + 0.1f * intensity;

    // Accumulate calories: METs * weight * time_hours
    // METs approximation: 1.0 + intensity/15 (rough mapping)
    unsigned long now = millis();
    if (last_update_ms > 0) {
        float dt_hours = (now - last_update_ms) / 3600000.0f;
        float mets = 1.0f + filtered_intensity / 15.0f;
        if (mets > 10.0f) mets = 10.0f;
        calories += mets * DEFAULT_WEIGHT_KG * dt_hours;
    }
    last_update_ms = now;
}

uint8_t motion_intensity_get(void) {
    return (uint8_t)(filtered_intensity + 0.5f);
}

float motion_intensity_get_mets(void) {
    float mets = 1.0f + filtered_intensity / 15.0f;
    if (mets > 10.0f) mets = 10.0f;
    return mets;
}

float motion_intensity_get_calories(void) {
    return calories;
}

void motion_intensity_reset_calories(void) {
    calories = 0.0f;
}
