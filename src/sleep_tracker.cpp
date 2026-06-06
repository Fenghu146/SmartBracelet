// Sleep tracker: uses motion intensity + time to estimate sleep stages
// Logic:
//   - Low intensity (<5) for 10+ minutes during night hours (22:00-07:00) = sleep
//   - Very low intensity (<2) sustained = deep sleep
//   - Low intensity (2-8) = light sleep
//   - Higher intensity or daytime = awake
#include "sleep_tracker.h"
#include <Arduino.h>
#include "debug_log.h"

#define SLEEP_ONSET_MINUTES    10   // 10 min of low motion to enter sleep
#define INTENSITY_DEEP_THRESH  2   // below this = deep sleep candidate
#define INTENSITY_LIGHT_THRESH 8   // below this = light sleep
#define NIGHT_START_HOUR       22   // sleep tracking active 22:00-07:00
#define NIGHT_END_HOUR         7
#define EPOCH_SECONDS          60   // classify every 60 seconds

static bool is_sleeping = false;
static sleep_stage_t current_stage = SLEEP_AWAKE;
static unsigned long low_motion_start = 0;
static unsigned long last_update_ms = 0;
static unsigned long last_epoch_ms = 0;

static int total_sleep_min = 0;
static int deep_min = 0;
static int light_min = 0;
static int awake_in_bed_min = 0;

// Rolling average of recent intensity for smoothing
static float avg_intensity = 50.0f;

void sleep_tracker_init(void) {
    is_sleeping = false;
    current_stage = SLEEP_AWAKE;
    low_motion_start = 0;
    last_update_ms = 0;
    last_epoch_ms = 0;
    total_sleep_min = 0;
    deep_min = 0;
    light_min = 0;
    awake_in_bed_min = 0;
    avg_intensity = 50.0f;
}

static bool is_night_hours(int hour) {
    return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
}

void sleep_tracker_update(uint8_t intensity, int hour, int minute) {
    unsigned long now = millis();

    // Smooth intensity with rolling average
    avg_intensity = 0.95f * avg_intensity + 0.05f * (float)intensity;

    // Initialize timestamps
    if (last_update_ms == 0) {
        last_update_ms = now;
        last_epoch_ms = now;
        return;
    }
    last_update_ms = now;

    // Only classify during night hours
    if (!is_night_hours(hour)) {
        if (is_sleeping) {
            // Woke up (morning)
            is_sleeping = false;
            current_stage = SLEEP_AWAKE;
            LOG_INFO("Sleep: ended. Total=%dm Deep=%dm Light=%dm",
                total_sleep_min, deep_min, light_min);
        }
        return;
    }

    // Classify every EPOCH_SECONDS
    if (now - last_epoch_ms < (unsigned long)(EPOCH_SECONDS * 1000)) return;
    last_epoch_ms = now;

    float smoothed = avg_intensity;

    if (!is_sleeping) {
        // Check for sleep onset
        if (smoothed < INTENSITY_LIGHT_THRESH) {
            if (low_motion_start == 0) {
                low_motion_start = now;
            }
            unsigned long elapsed = (now - low_motion_start) / 60000;
            if (elapsed >= SLEEP_ONSET_MINUTES) {
                is_sleeping = true;
                current_stage = SLEEP_LIGHT;
                LOG_INFO("Sleep: onset detected (avg intensity=%.1f)", smoothed);
            }
        } else {
            low_motion_start = 0;
        }
    } else {
        // Already sleeping - classify stage
        if (smoothed < INTENSITY_DEEP_THRESH) {
            current_stage = SLEEP_DEEP;
            deep_min++;
            total_sleep_min++;
        } else if (smoothed < INTENSITY_LIGHT_THRESH) {
            current_stage = SLEEP_LIGHT;
            light_min++;
            total_sleep_min++;
        } else {
            // Brief awakening during sleep
            current_stage = SLEEP_AWAKE;
            awake_in_bed_min++;
            // If awake for too long, consider sleep ended
            if (awake_in_bed_min > 30) {
                LOG_INFO("Sleep: extended wake period, ending session");
                is_sleeping = false;
                awake_in_bed_min = 0;
            }
        }
    }
}

bool sleep_tracker_is_sleeping(void) {
    return is_sleeping;
}

sleep_stage_t sleep_tracker_get_stage(void) {
    return current_stage;
}

int sleep_tracker_get_total_minutes(void) {
    return total_sleep_min;
}

int sleep_tracker_get_deep_minutes(void) {
    return deep_min;
}

int sleep_tracker_get_light_minutes(void) {
    return light_min;
}

void sleep_tracker_reset(void) {
    total_sleep_min = 0;
    deep_min = 0;
    light_min = 0;
    awake_in_bed_min = 0;
    is_sleeping = false;
    current_stage = SLEEP_AWAKE;
    low_motion_start = 0;
}
