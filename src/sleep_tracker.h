#ifndef SLEEP_TRACKER_H
#define SLEEP_TRACKER_H

#include <stdint.h>
#include <stdbool.h>

// Sleep stages
typedef enum {
    SLEEP_AWAKE = 0,
    SLEEP_LIGHT = 1,
    SLEEP_DEEP  = 2
} sleep_stage_t;

// Initialize sleep tracker
void sleep_tracker_init(void);

// Feed motion intensity (0-100) at ~1Hz
void sleep_tracker_update(uint8_t intensity, int hour, int minute);

// Query sleep state
bool sleep_tracker_is_sleeping(void);
sleep_stage_t sleep_tracker_get_stage(void);
int  sleep_tracker_get_total_minutes(void);
int  sleep_tracker_get_deep_minutes(void);
int  sleep_tracker_get_light_minutes(void);

// Reset for new night
void sleep_tracker_reset(void);

#endif // SLEEP_TRACKER_H
