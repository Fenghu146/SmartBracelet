// Step counter with improved algorithm: low-pass filter + adaptive baseline + time validation
#include "step_counter.h"
#include <Arduino.h>
#include <math.h>

static const float STEP_LOCK_MS = 250;
static const float STEP_TIMEOUT_MS = 2000;

static int step_count = 0;
static float step_filt_mag = 1.0f;
static float step_baseline = 1.0f;
static int step_baseline_samples = 0;
static unsigned long last_step_time = 0;
static bool step_in_peak = false;

void step_counter_init(void) {
    step_count = 0;
    step_filt_mag = 1.0f;
    step_baseline = 1.0f;
    step_baseline_samples = 0;
    last_step_time = 0;
    step_in_peak = false;
}

void step_counter_update(float ax, float ay, float az) {
    float raw = sqrtf(ax * ax + ay * ay + az * az);
    step_filt_mag = 0.85f * step_filt_mag + 0.15f * raw;

    // Adaptive baseline: running average
    if (step_baseline_samples < 50) {
        step_baseline = 0.98f * step_baseline + 0.02f * step_filt_mag;
        step_baseline_samples++;
    } else if (fabs(step_filt_mag - step_baseline) < 0.05f) {
        step_baseline = 0.99f * step_baseline + 0.01f * step_filt_mag;
    }

    float threshold = step_baseline + 0.35f;
    unsigned long now = millis();

    if (!step_in_peak && step_filt_mag > threshold &&
        now - last_step_time > STEP_LOCK_MS) {
        step_count++;
        step_in_peak = true;
        last_step_time = now;
    }

    if (step_in_peak && step_filt_mag < step_baseline + 0.05f) {
        step_in_peak = false;
    }

    // Reset if no step for too long
    if (now - last_step_time > STEP_TIMEOUT_MS) {
        step_in_peak = false;
    }
}

int step_counter_get(void) {
    return step_count;
}

void step_counter_reset(void) {
    step_count = 0;
    step_filt_mag = 1.0f;
    step_baseline = 1.0f;
    step_baseline_samples = 0;
    step_in_peak = false;
}

void step_counter_set(int count) {
    step_count = count;
}
