// Battery health monitoring: tracks cycles and estimates degradation
#include "batt_health.h"
#include "nvs_store.h"
#include "debug_log.h"

// State
static int cycles = 0;
static uint16_t first_full_mv = 0;  // First recorded full-charge voltage
static uint16_t last_full_mv = 0;   // Most recent full-charge voltage
static bool was_charging = false;
static uint16_t peak_mv = 0;        // Peak voltage during current charge

// Battery voltage thresholds
#define BATT_FULL_MV     4300   // Nominal full charge
#define BATT_EMPTY_MV    3300   // Nominal empty
#define BATT_NEW_FULL_MV 4350   // Brand new battery full charge

void batt_health_init(void) {
    cycles = nvs_get_batt_cycles();
    last_full_mv = nvs_get_batt_full_mv();
    first_full_mv = last_full_mv;  // Use saved as baseline if no separate first

    // Load first_full_mv from NVS (stored in steps_yesterday repurposed? No, use dedicated)
    // Actually, use batt_full_mv as both first and last for now
    if (first_full_mv == 0 || first_full_mv == 4200) {
        first_full_mv = BATT_NEW_FULL_MV;  // Default for new battery
    }

    LOG_INFO("Battery health: cycles=%d, full_mv=%d", cycles, last_full_mv);
}

void batt_health_update(bool is_charging, uint16_t batt_mv) {
    // Track peak voltage during charging
    if (is_charging && batt_mv > 0) {
        if (batt_mv > peak_mv) {
            peak_mv = batt_mv;
        }
    }

    // Detect charge cycle: charging -> not charging
    if (was_charging && !is_charging && peak_mv > BATT_FULL_MV - 100) {
        // Charge just completed, record full-charge voltage
        if (peak_mv > 0) {
            last_full_mv = peak_mv;
            nvs_set_batt_full_mv(last_full_mv);

            // If this is the first cycle, set as baseline
            if (cycles == 0 || first_full_mv == BATT_NEW_FULL_MV) {
                first_full_mv = peak_mv;
            }
        }
        cycles++;
        nvs_set_batt_cycles(cycles);
        LOG_INFO("Battery: cycle #%d detected, full_mv=%d", cycles, last_full_mv);
        peak_mv = 0;
    }

    // Reset peak when starting a new charge session
    if (!was_charging && is_charging) {
        peak_mv = 0;
    }

    was_charging = is_charging;
}

int batt_health_get_cycles(void) {
    return cycles;
}

int batt_health_get_percent(void) {
    if (last_full_mv == 0 || first_full_mv == 0) return 100;

    // Health = (current_full_mv - empty_mv) / (first_full_mv - empty_mv) * 100
    // A new battery charges to ~4350mV, degrades over time
    int current_range = last_full_mv - BATT_EMPTY_MV;
    int original_range = first_full_mv - BATT_EMPTY_MV;

    if (original_range <= 0) return 100;

    int health = (current_range * 100) / original_range;
    if (health > 100) health = 100;
    if (health < 0) health = 0;
    return health;
}

uint16_t batt_health_get_full_mv(void) {
    return last_full_mv;
}
