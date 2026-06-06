#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

// OTA state machine
typedef enum {
    OTA_IDLE = 0,
    OTA_DOWNLOADING,
    OTA_WRITING,
    OTA_VERIFYING,
    OTA_SUCCESS,
    OTA_ERROR
} ota_state_t;

// Start OTA from HTTP URL (WiFi must be connected)
// Returns true if download started. Download runs on its own FreeRTOS task (Core 0).
bool ota_start(const char *url);

// Get current OTA state (thread-safe)
ota_state_t ota_get_state(void);

// Get download progress (0-100)
int ota_get_progress(void);

// Get error message (valid when state == OTA_ERROR)
const char* ota_get_error(void);

// Check if OTA restart is pending (call from main loop)
bool ota_check_restart(void);

// Firmware version string (embedded at build time)
#define FIRMWARE_VERSION "1.0.0"

#endif
