#include "ble_srv.h"
#include "pin_config.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>

#define DEVICE_NAME "SmartBracelet"
#define MANUFACTURER "Waveshare"
#define MODEL_NUM "ESP32-S3-Touch-LCD-1.83"
#define SERIAL_NUM "SN-001"

static BLEServer *pServer = nullptr;
static BLEService *pTimeService = nullptr;
static BLECharacteristic *pTimeChar = nullptr;
static BLECharacteristic *pBatteryChar = nullptr;
static BLECharacteristic *pNotifyChar = nullptr;

NotificationData ble_notification = {0};

class ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *srv, esp_ble_gatts_cb_param_t *param)
    {
        USBSerial.println("BLE: connected");
    }

    void onDisconnect(BLEServer *srv)
    {
        USBSerial.println("BLE: disconnected");
        pServer->startAdvertising();
    }
};

class NotifyCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *chr)
    {
        std::string val = chr->getValue();
        if (val.length() == 0) return;
        ble_notification.has_new = 1;

        int p1 = val.find('|');
        int p2 = val.find('|', p1 + 1);
        if (p1 > 0 && p1 < 16) {
            strncpy(ble_notification.app_id, val.substr(0, p1).c_str(), 15);
        }
        if (p2 > p1 && p2 - p1 - 1 < 64) {
            strncpy(ble_notification.title,
                val.substr(p1 + 1, p2 - p1 - 1).c_str(), 63);
        }
        if (p2 > 0 && val.length() - p2 - 1 < 128) {
            strncpy(ble_notification.body,
                val.substr(p2 + 1).c_str(), 127);
        }

        USBSerial.printf("BLE notify: %s | %s | %s\n",
            ble_notification.app_id, ble_notification.title,
            ble_notification.body);
    }
};

static void setup_device_info(BLEServer *server)
{
    BLEService *svc = server->createService(BLEUUID((uint16_t)0x180A));
    BLECharacteristic *c;

    c = svc->createCharacteristic(BLEUUID((uint16_t)0x2A29),
        BLECharacteristic::PROPERTY_READ);
    c->setValue(MANUFACTURER);

    c = svc->createCharacteristic(BLEUUID((uint16_t)0x2A24),
        BLECharacteristic::PROPERTY_READ);
    c->setValue(MODEL_NUM);

    c = svc->createCharacteristic(BLEUUID((uint16_t)0x2A25),
        BLECharacteristic::PROPERTY_READ);
    c->setValue(SERIAL_NUM);

    svc->start();
}

static void setup_battery_service(BLEServer *server)
{
    BLEService *svc = server->createService(BLEUUID((uint16_t)0x180F));
    pBatteryChar = svc->createCharacteristic(
        BLEUUID((uint16_t)0x2A19),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY);
    pBatteryChar->addDescriptor(new BLE2902());
    int batt_init = 100;
    pBatteryChar->setValue(batt_init);
    svc->start();
}

static void setup_current_time(BLEServer *server)
{
    pTimeService = server->createService(BLEUUID((uint16_t)0x1805));
    pTimeChar = pTimeService->createCharacteristic(
        BLEUUID((uint16_t)0x2A2B),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE);
    pTimeService->start();
}

static void setup_notification_service(BLEServer *server)
{
    BLEService *svc = server->createService(
        BLEUUID("abcd0001-0000-1000-8000-00805f9b34fb"));
    pNotifyChar = svc->createCharacteristic(
        BLEUUID("abcd0002-0000-1000-8000-00805f9b34fb"),
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR);
    pNotifyChar->setCallbacks(new NotifyCallback());
    svc->start();
}

void ble_srv_init(void)
{
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_NO_MITM);
    BLESecurity *sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
    sec->setCapability(ESP_IO_CAP_NONE);
    sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    setup_device_info(pServer);
    setup_battery_service(pServer);
    setup_current_time(pServer);
    setup_notification_service(pServer);

    BLEAdvertising *adv = pServer->getAdvertising();
    adv->addServiceUUID(BLEUUID((uint16_t)0x180F));
    adv->addServiceUUID(BLEUUID((uint16_t)0x1805));
    adv->addServiceUUID(BLEUUID("abcd0001-0000-1000-8000-00805f9b34fb"));
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    USBSerial.println("BLE: advertising");
}

void ble_srv_update_battery(uint8_t level)
{
    if (pBatteryChar) {
        uint8_t batt_val = level;
        pBatteryChar->setValue(&batt_val, 1);
        pBatteryChar->notify();
    }
}

void ble_srv_update_time(void)
{
    if (!pTimeChar) return;
    time_t now = time(nullptr);
    if (now < 100000) return;

    struct tm *ti = localtime(&now);
    uint8_t cts[10] = {0};
    cts[0] = ti->tm_year % 100;
    cts[1] = (ti->tm_year + 1900) / 100;
    cts[2] = ti->tm_mon + 1;
    cts[3] = ti->tm_mday;
    cts[4] = ti->tm_hour;
    cts[5] = ti->tm_min;
    cts[6] = ti->tm_sec;
    cts[7] = 1;
    cts[8] = 0;
    cts[9] = 0;
    pTimeChar->setValue(cts, 10);
}
