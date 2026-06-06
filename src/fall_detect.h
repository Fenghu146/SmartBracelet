#ifndef FALL_DETECT_H
#define FALL_DETECT_H

#include <Arduino.h>

// Fall detection state machine
typedef enum {
    FALL_MONITORING = 0,   // Normal monitoring
    FALL_FREEFALL,         // Free-fall detected (< 0.5g)
    FALL_IMPACT,           // Impact detected (> 3g)
    FALL_MOTIONLESS,       // No significant motion after impact
    FALL_CONFIRMED,        // Fall confirmed - alert!
    FALL_ALERT_SENT        // Alert has been sent
} fall_state_t;

// Initialize fall detection
void fall_detect_init(void);

// Push new IMU sample (acceleration in g)
void fall_detect_update(float ax, float ay, float az);

// Get current fall detection state
fall_state_t fall_detect_get_state(void);

// Check if a fall has been confirmed (call once, resets after read)
bool fall_detect_has_fallen(void);

// Acknowledge/reset fall alert (e.g. user pressed button)
void fall_detect_acknowledge(void);

#endif
