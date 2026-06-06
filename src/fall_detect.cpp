// Fall detection using IMU acceleration thresholds
// Algorithm: detect free-fall → impact → motionless sequence
// Based on typical fall signature: sudden drop (<0.5g) → impact (>3g) → stillness

#include "fall_detect.h"
#include "debug_log.h"
#include <math.h>

// Thresholds (in g units)
#define FREEFALL_THRESHOLD     0.5f   // Acceleration below this = free fall
#define IMPACT_THRESHOLD       3.0f   // Acceleration above this = impact
#define MOTIONLESS_THRESHOLD   0.3f   // Jerk below this = motionless
#define FREEFALL_MIN_MS        100    // Minimum free-fall duration (ms)
#define IMPACT_WINDOW_MS       1000   // Must impact within 1s of free-fall
#define MOTIONLESS_MIN_MS      3000   // Must be still for 3s after impact
#define ALERT_TIMEOUT_MS       30000  // Alert auto-dismiss after 30s

static fall_state_t fall_state = FALL_MONITORING;
static unsigned long state_enter_time = 0;
static bool fall_flag = false;

// Low-pass filtered acceleration magnitude
static float accel_mag_filt = 1.0f;

// Stillness tracking for post-impact motionless detection
static unsigned long still_start = 0;

void fall_detect_init(void) {
    fall_state = FALL_MONITORING;
    fall_flag = false;
    LOG_INFO("FallDetect: initialized");
}

fall_state_t fall_detect_get_state(void) {
    return fall_state;
}

bool fall_detect_has_fallen(void) {
    if (fall_flag) {
        fall_flag = false;
        return true;
    }
    return false;
}

void fall_detect_acknowledge(void) {
    if (fall_state == FALL_CONFIRMED || fall_state == FALL_ALERT_SENT) {
        fall_state = FALL_MONITORING;
        LOG_INFO("FallDetect: acknowledged, resuming monitoring");
    }
}

static void enter_state(fall_state_t new_state) {
    fall_state = new_state;
    state_enter_time = millis();
    // Reset stillness tracking when transitioning states
    if (new_state == FALL_MONITORING) {
        still_start = 0;
    }
}

void fall_detect_update(float ax, float ay, float az) {
    float mag = sqrtf(ax * ax + ay * ay + az * az);

    // Low-pass filter acceleration magnitude (time constant ~0.2s at 100Hz)
    accel_mag_filt = 0.95f * accel_mag_filt + 0.05f * mag;

    // Jerk (rate of change of magnitude)
    static float prev_mag = 1.0f;
    float jerk = fabsf(mag - prev_mag);
    prev_mag = mag;

    unsigned long now = millis();
    unsigned long elapsed = now - state_enter_time;

    switch (fall_state) {
        case FALL_MONITORING:
            // Detect free-fall: acceleration drops below threshold
            if (accel_mag_filt < FREEFALL_THRESHOLD) {
                enter_state(FALL_FREEFALL);
                LOG_DEBUG("FallDetect: free-fall detected (%.2fg)", accel_mag_filt);
            }
            break;

        case FALL_FREEFALL:
            // Check if free-fall persists for minimum duration
            if (accel_mag_filt >= FREEFALL_THRESHOLD) {
                // Brief drop, not a real free-fall - go back to monitoring
                // But first check if this was an impact
                if (mag > IMPACT_THRESHOLD) {
                    enter_state(FALL_IMPACT);
                    LOG_DEBUG("FallDetect: impact detected (%.2fg)", mag);
                } else {
                    enter_state(FALL_MONITORING);
                }
            } else if (elapsed > FREEFALL_MIN_MS) {
                // Sustained free-fall, wait for impact
                LOG_VERBOSE("FallDetect: sustained free-fall, waiting impact...");
            }
            break;

        case FALL_IMPACT:
            // After impact, check for motionlessness
            // If we see another impact or significant motion, it might be recovery
            if (elapsed > IMPACT_WINDOW_MS) {
                // No motionless period - likely recovered from stumble
                enter_state(FALL_MONITORING);
                LOG_INFO("FallDetect: impact timeout, no fall confirmed");
            } else if (jerk < MOTIONLESS_THRESHOLD) {
                // Check for sustained motionlessness (still_start is file-level static)
                if (jerk < MOTIONLESS_THRESHOLD * 0.5f) {
                    if (still_start == 0) still_start = now;
                    if (now - still_start > MOTIONLESS_MIN_MS) {
                        enter_state(FALL_MOTIONLESS);
                        LOG_DEBUG("FallDetect: motionless after impact");
                        still_start = 0;
                    }
                } else {
                    still_start = 0;
                }
            } else {
                still_start = 0;
            }
            break;

        case FALL_MOTIONLESS:
            // If significant motion resumes, person is moving (not a fall)
            if (jerk > MOTIONLESS_THRESHOLD * 2) {
                enter_state(FALL_MONITORING);
                LOG_INFO("FallDetect: motion resumed, not a fall");
            } else if (elapsed > MOTIONLESS_MIN_MS) {
                // Confirmed fall: motionless for extended period after impact
                enter_state(FALL_CONFIRMED);
                fall_flag = true;
                LOG_INFO("FallDetect: *** FALL CONFIRMED ***");
            }
            break;

        case FALL_CONFIRMED:
            // Auto-dismiss after timeout
            if (elapsed > ALERT_TIMEOUT_MS) {
                enter_state(FALL_MONITORING);
                LOG_INFO("FallDetect: alert timeout, resuming monitoring");
            }
            break;

        case FALL_ALERT_SENT:
            // Alert was sent, wait for acknowledgment
            if (elapsed > ALERT_TIMEOUT_MS) {
                enter_state(FALL_MONITORING);
                LOG_INFO("FallDetect: alert timeout, resuming monitoring");
            }
            break;
    }
}
