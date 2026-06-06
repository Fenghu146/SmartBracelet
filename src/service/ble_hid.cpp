// BLE HID Consumer Control service for media playback
// Implements a minimal HID over GATT profile for Play/Pause, Next, Prev, Vol+, Vol-

#include "ble_hid.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// HID Service UUID
#define HID_SERVICE_UUID                "1812"
#define HID_REPORT_CHAR_UUID            "2A4D"
#define HID_REPORT_MAP_CHAR_UUID        "2A4B"
#define HID_INFO_CHAR_UUID              "2A4A"
#define HID_CONTROL_CHAR_UUID           "2A4C"
#define HID_PROTOCOL_CHAR_UUID          "2A4E"

// HID Consumer Control report IDs
#define REPORT_ID_CONSUMER  0x03

// Consumer Control key codes (2 bytes, little-endian)
#define HID_CC_PLAY_PAUSE   0x00CD
#define HID_CC_NEXT         0x00B5
#define HID_CC_PREV         0x00B6
#define HID_CC_VOL_UP       0x00E9
#define HID_CC_VOL_DOWN     0x00EA

static BLECharacteristic *pReportChar = nullptr;
static BLECharacteristic *pReportMapChar = nullptr;
static bool hid_connected = false;

// HID Report Descriptor for Consumer Control
static const uint8_t reportMapData[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_CONSUMER,  // Report ID (3)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x19, 0x00,        // Usage Minimum (0)
    0x2A, 0xFF, 0x00,  // Usage Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x02,        // Report Count (2)
    0x81, 0x00,        // Input (Data, Array)
    0xC0               // End Collection
};

// HID Information: version=1.11, flags=0, country=0
static const uint8_t hidInfoData[] = {0x11, 0x01, 0x00, 0x02};

static void send_consumer_key(uint16_t key) {
    if (!pReportChar || !hid_connected) return;

    // Send key press
    uint8_t report[3] = {REPORT_ID_CONSUMER, (uint8_t)(key & 0xFF), (uint8_t)((key >> 8) & 0xFF)};
    pReportChar->setValue(report, 3);
    pReportChar->notify();

    delay(50);

    // Send key release
    uint8_t release[3] = {REPORT_ID_CONSUMER, 0x00, 0x00};
    pReportChar->setValue(release, 3);
    pReportChar->notify();
}

void ble_hid_init(void *bleServer) {
    BLEServer *pServer = static_cast<BLEServer *>(bleServer);
    if (!pServer) {
        USBSerial.println("HID: no BLE server provided");
        return;
    }

    BLEService *pHidService = pServer->createService(BLEUUID(HID_SERVICE_UUID));

    // Report Map characteristic
    pReportMapChar = pHidService->createCharacteristic(
        BLEUUID(HID_REPORT_MAP_CHAR_UUID),
        BLECharacteristic::PROPERTY_READ);
    pReportMapChar->setValue((uint8_t *)reportMapData, sizeof(reportMapData));

    // Report characteristic (for sending key presses)
    pReportChar = pHidService->createCharacteristic(
        BLEUUID(HID_REPORT_CHAR_UUID),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY);
    pReportChar->addDescriptor(new BLE2902());

    // HID Information
    BLECharacteristic *pInfoChar = pHidService->createCharacteristic(
        BLEUUID(HID_INFO_CHAR_UUID),
        BLECharacteristic::PROPERTY_READ);
    pInfoChar->setValue((uint8_t *)hidInfoData, sizeof(hidInfoData));

    // HID Control Point
    BLECharacteristic *pCtrlChar = pHidService->createCharacteristic(
        BLEUUID(HID_CONTROL_CHAR_UUID),
        BLECharacteristic::PROPERTY_WRITE_NR);
    uint8_t ctrl = 0;
    pCtrlChar->setValue(&ctrl, 1);

    // Protocol Mode
    BLECharacteristic *pProtoChar = pHidService->createCharacteristic(
        BLEUUID(HID_PROTOCOL_CHAR_UUID),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR);
    uint8_t proto = 1;  // Report Protocol Mode
    pProtoChar->setValue(&proto, 1);

    pHidService->start();
    USBSerial.println("HID: Consumer Control service started");
}

bool ble_hid_is_ready(void) {
    return hid_connected;
}

void ble_hid_play_pause(void) {
    USBSerial.println("HID: Play/Pause");
    send_consumer_key(HID_CC_PLAY_PAUSE);
}

void ble_hid_next_track(void) {
    USBSerial.println("HID: Next Track");
    send_consumer_key(HID_CC_NEXT);
}

void ble_hid_prev_track(void) {
    USBSerial.println("HID: Previous Track");
    send_consumer_key(HID_CC_PREV);
}

void ble_hid_volume_up(void) {
    USBSerial.println("HID: Volume Up");
    send_consumer_key(HID_CC_VOL_UP);
}

void ble_hid_volume_down(void) {
    USBSerial.println("HID: Volume Down");
    send_consumer_key(HID_CC_VOL_DOWN);
}
