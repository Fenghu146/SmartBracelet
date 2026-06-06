#ifndef BLE_SRV_H
#define BLE_SRV_H

#include <Arduino.h>

void ble_srv_init(void);
void ble_srv_update_battery(uint8_t level);
void ble_srv_update_time(void);
void ble_srv_send(const char *data);
bool ble_is_connected(void);

// Data service — phone reads watch telemetry
void ble_srv_update_steps(uint32_t steps);
void ble_srv_update_batt_raw(uint16_t mv);
void ble_srv_update_activity(uint8_t act);

// Exported so main.cpp can set them; ble_srv will notify if connected
extern uint32_t ble_steps;
extern uint16_t ble_batt_raw;
extern int  ble_activity;  // 0=walk 1=run 2=idle

typedef struct {
    char app_id[16];
    char title[64];
    char body[128];
    uint8_t has_new;
} NotificationData;

extern NotificationData ble_notification;

// Voice chat: callback when phone writes voice command
typedef void (*voice_cmd_callback_t)(const char *cmd, const char *arg);
void ble_srv_set_voice_cmd_callback(voice_cmd_callback_t cb);
void ble_srv_send_voice_result(const char *transcription, const char *response);

// OTA service — phone triggers firmware update via BLE
void ble_srv_update_ota_state(uint8_t state, uint8_t progress);

// Get the BLE server pointer (for HID service to attach to)
void* ble_srv_get_server(void);

// Do Not Disturb mode
void ble_srv_set_dnd(bool enable);
bool ble_srv_get_dnd(void);

// IMU feature data for AI co-inference (12 floats: mean[6] + std[6])
void ble_srv_update_imu_features(const float *features, int count);

#endif
