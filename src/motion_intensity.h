#ifndef MOTION_INTENSITY_H
#define MOTION_INTENSITY_H

#include <stdint.h>

// Initialize motion intensity tracker
void motion_intensity_init(void);

// Feed new IMU sample (call at IMU sample rate)
void motion_intensity_update(float ax, float ay, float az);

// Get current intensity level (0-100)
// 0 = resting, 100 = very intense activity
uint8_t motion_intensity_get(void);

// Get estimated METs (Metabolic Equivalent of Task)
// 1.0 = resting, 3-6 = moderate, 6+ = vigorous
float motion_intensity_get_mets(void);

// Get estimated calories burned since last reset
float motion_intensity_get_calories(void);

// Reset calorie counter (call at daily reset)
void motion_intensity_reset_calories(void);

#endif // MOTION_INTENSITY_H
