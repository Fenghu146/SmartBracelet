#ifndef BATT_HEALTH_H
#define BATT_HEALTH_H

#include <stdint.h>
#include <stdbool.h>

// Initialize battery health tracking (loads from NVS)
void batt_health_init(void);

// Update battery state (call periodically with charging status and voltage)
// is_charging: true if currently charging
// batt_mv: battery voltage in mV (0 if invalid/USB only)
void batt_health_update(bool is_charging, uint16_t batt_mv);

// Get number of charge cycles detected
int batt_health_get_cycles(void);

// Get estimated health percentage (0-100)
// Based on full-charge voltage degradation
int batt_health_get_percent(void);

// Get last recorded full-charge voltage
uint16_t batt_health_get_full_mv(void);

#endif // BATT_HEALTH_H
