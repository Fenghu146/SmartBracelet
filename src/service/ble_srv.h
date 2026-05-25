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

#endif
