// BLE HID Consumer Control service for media playback
// Initializes after ble_srv_init(), shares the same BLE server

#ifndef BLE_HID_H
#define BLE_HID_H

#include <Arduino.h>

// Must be called after ble_srv_init() — uses the same BLE server
// Pass the server pointer from ble_srv
struct BLEServer;
void ble_hid_init(void *bleServer);

bool ble_hid_is_ready(void);

// Send media control commands
void ble_hid_play_pause(void);
void ble_hid_next_track(void);
void ble_hid_prev_track(void);
void ble_hid_volume_up(void);
void ble_hid_volume_down(void);

#endif
