#ifndef BLE_SRV_H
#define BLE_SRV_H

#include <Arduino.h>

void ble_srv_init(void);
void ble_srv_update_battery(uint8_t level);
void ble_srv_update_time(void);

typedef struct {
    char app_id[16];
    char title[64];
    char body[128];
    uint8_t has_new;
} NotificationData;

extern NotificationData ble_notification;

#endif
