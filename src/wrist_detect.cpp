// Wrist raise detection using gravity vector orientation change + motion
#include "wrist_detect.h"
#include <Arduino.h>
#include <math.h>

static float grav_x = 0, grav_y = 0, grav_z = 0;
static float rest_grav_x = 0, rest_grav_y = 0, rest_grav_z = 0;
static bool rest_ready = false;
static int rest_count = 0;
static unsigned long wrist_raise_time = 0;
static bool wrist_is_raised = false;

static wrist_raise_callback_t raise_callback = nullptr;

void wrist_detect_init(void) {
    grav_x = grav_y = grav_z = 0;
    rest_grav_x = rest_grav_y = rest_grav_z = 0;
    rest_ready = false;
    rest_count = 0;
    wrist_raise_time = 0;
    wrist_is_raised = false;
}

void wrist_detect_set_callback(wrist_raise_callback_t cb) {
    raise_callback = cb;
}

bool wrist_detect_is_raised(void) {
    return wrist_is_raised;
}

void wrist_detect_update(float ax, float ay, float az) {
    // Gravity vector (low-pass filter, ~0.5s time constant at 200Hz)
    float alpha = 0.02f;
    grav_x = (1 - alpha) * grav_x + alpha * ax;
    grav_y = (1 - alpha) * grav_y + alpha * ay;
    grav_z = (1 - alpha) * grav_z + alpha * az;

    // Motion level: high-pass energy in raw acceleration
    static float prev_raw = 1.0f;
    float raw = sqrtf(ax * ax + ay * ay + az * az);
    float jerk = fabsf(raw - prev_raw);
    prev_raw = raw;
    static float motion = 0;
    motion = 0.8f * motion + 0.2f * jerk;

    // Update rest gravity baseline when stationary
    if (motion < 0.03f) {
        rest_count++;
        if (rest_count > 100 && !rest_ready) {
            rest_grav_x = grav_x; rest_grav_y = grav_y; rest_grav_z = grav_z;
            rest_ready = true;
            rest_count = 101;
        } else if (rest_count > 300) {
            rest_grav_x = 0.98f * rest_grav_x + 0.02f * grav_x;
            rest_grav_y = 0.98f * rest_grav_y + 0.02f * grav_y;
            rest_grav_z = 0.98f * rest_grav_z + 0.02f * grav_z;
        }
    } else {
        rest_count = 0;
    }

    if (!rest_ready || wrist_is_raised) {
        if (wrist_is_raised && millis() - wrist_raise_time > 3000)
            wrist_is_raised = false;
        return;
    }

    // Gravity angle between current and rest
    float dot = grav_x * rest_grav_x + grav_y * rest_grav_y + grav_z * rest_grav_z;
    float g = sqrtf(grav_x * grav_x + grav_y * grav_y + grav_z * grav_z);
    float r = sqrtf(rest_grav_x * rest_grav_x + rest_grav_y * rest_grav_y + rest_grav_z * rest_grav_z);
    if (g < 0.01f || r < 0.01f) return;

    float cos_a = dot / (g * r);
    if (cos_a > 1.0f) cos_a = 1.0f;
    if (cos_a < -1.0f) cos_a = -1.0f;
    float angle = acosf(cos_a) * 57.2958f; // rad to deg

    // Wrist raise = motion + orientation change
    if (motion > 0.08f && angle > 25.0f) {
        wrist_is_raised = true;
        wrist_raise_time = millis();
        if (raise_callback) raise_callback();
    }
}
