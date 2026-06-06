#include "ble_srv.h"
#include "pin_config.h"
#include "ota_update.h"
#include "../debug_log.h"
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
static BLECharacteristic *pTxChar = nullptr;

// Data service characteristics
static BLECharacteristic *pStepChar = nullptr;
static BLECharacteristic *pBattRawChar = nullptr;
static BLECharacteristic *pActChar = nullptr;
static BLECharacteristic *pImuFeatChar = nullptr;
#define IMU_FEAT_CHAR_UUID     "abcd1004-0000-1000-8000-00805f9b34fb"

NotificationData ble_notification = {0};
uint32_t ble_steps = 0;
uint16_t ble_batt_raw = 0;
int  ble_activity = 0;

// Voice command callback
static voice_cmd_callback_t voice_cmd_cb = NULL;

// OTA service characteristics
static BLECharacteristic *pOtaCtrlChar = nullptr;
static BLECharacteristic *pOtaStateChar = nullptr;
#define OTA_SERVICE_UUID       "abcd2000-0000-1000-8000-00805f9b34fb"
#define OTA_CTRL_CHAR_UUID     "abcd2001-0000-1000-8000-00805f9b34fb"
#define OTA_STATE_CHAR_UUID    "abcd2002-0000-1000-8000-00805f9b34fb"

// Do Not Disturb
static bool dnd_enabled = false;

void ble_srv_set_voice_cmd_callback(voice_cmd_callback_t cb) { voice_cmd_cb = cb; }

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
        LOG_DEBUG("BLE RX: len=%d data='%s'", val.length(), val.c_str());
        if (val.length() == 0) return;

        // Voice command protocol: "voice:cmd|arg"
        if (val.rfind("voice:", 0) == 0 && voice_cmd_cb) {
            std::string payload = val.substr(6);  // after "voice:"
            int sep = payload.find('|');
            std::string cmd = (sep >= 0) ? payload.substr(0, sep) : payload;
            std::string arg = (sep >= 0) ? payload.substr(sep + 1) : "";
            USBSerial.printf("BLE voice: cmd=%s arg=%s\n", cmd.c_str(), arg.c_str());
            voice_cmd_cb(cmd.c_str(), arg.c_str());
            return;
        }

        // OTA command protocol: "ota:url"
        if (val.rfind("ota:", 0) == 0) {
            std::string url = val.substr(4);
            LOG_INFO("BLE OTA: url=%s", url.c_str());
            if (ota_start(url.c_str())) {
                LOG_INFO("OTA: download started");
            } else {
                LOG_ERR("OTA: start failed: %s", ota_get_error());
            }
            return;
        }

        // DND command protocol: "dnd:1" or "dnd:0"
        if (val.rfind("dnd:", 0) == 0) {
            std::string dnd_val = val.substr(4);
            dnd_enabled = (dnd_val == "1" || dnd_val == "true");
            LOG_INFO("BLE: DND %s", dnd_enabled ? "ON" : "OFF");
            return;
        }

        // Notification format: "app_id|title|body"
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

    pTxChar = svc->createCharacteristic(
        BLEUUID("abcd0003-0000-1000-8000-00805f9b34fb"),
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE |
        BLECharacteristic::PROPERTY_READ);
    pTxChar->addDescriptor(new BLE2902());
    pTxChar->setValue("");

    svc->start();
}

// ── Data service: watch → phone telemetry ──────────────────────
static void setup_data_service(BLEServer *server)
{
    BLEService *svc = server->createService(
        BLEUUID("abcd1000-0000-1000-8000-00805f9b34fb"));

    pStepChar = svc->createCharacteristic(
        BLEUUID("abcd1001-0000-1000-8000-00805f9b34fb"),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY);
    pStepChar->addDescriptor(new BLE2902());
    pStepChar->setValue(ble_steps);

    pBattRawChar = svc->createCharacteristic(
        BLEUUID("abcd1002-0000-1000-8000-00805f9b34fb"),
        BLECharacteristic::PROPERTY_READ);
    pBattRawChar->setValue(ble_batt_raw);

    pActChar = svc->createCharacteristic(
        BLEUUID("abcd1003-0000-1000-8000-00805f9b34fb"),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY);
    pActChar->addDescriptor(new BLE2902());
    pActChar->setValue(ble_activity);

    // IMU features for AI co-inference (12 x float32 = 48 bytes)
    pImuFeatChar = svc->createCharacteristic(
        BLEUUID(IMU_FEAT_CHAR_UUID),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY);
    pImuFeatChar->addDescriptor(new BLE2902());

    svc->start();
    LOG_INFO("BLE: DataService started");
}

// ── OTA service: phone triggers firmware update ──────────────
static void setup_ota_service(BLEServer *server)
{
    BLEService *svc = server->createService(BLEUUID(OTA_SERVICE_UUID));

    // Control characteristic: phone writes "ota:<url>" to start update
    pOtaCtrlChar = svc->createCharacteristic(
        BLEUUID(OTA_CTRL_CHAR_UUID),
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR);
    pOtaCtrlChar->setCallbacks(new NotifyCallback());  // reuse same parser

    // State characteristic: watch notifies phone with [state, progress]
    pOtaStateChar = svc->createCharacteristic(
        BLEUUID(OTA_STATE_CHAR_UUID),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY);
    pOtaStateChar->addDescriptor(new BLE2902());
    uint8_t init_state[2] = {0, 0};  // IDLE, 0%
    pOtaStateChar->setValue(init_state, 2);

    svc->start();
    LOG_INFO("BLE: OTA Service started");
}

void ble_srv_init(void)
{
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_NO_MITM);
    BLEDevice::setMTU(256);
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
    setup_data_service(pServer);
    setup_ota_service(pServer);

    BLEAdvertising *adv = pServer->getAdvertising();
    adv->addServiceUUID(BLEUUID((uint16_t)0x180F));
    adv->addServiceUUID(BLEUUID((uint16_t)0x1805));
    adv->addServiceUUID(BLEUUID("abcd0001-0000-1000-8000-00805f9b34fb"));
    adv->addServiceUUID(BLEUUID("abcd1000-0000-1000-8000-00805f9b34fb"));
    adv->addServiceUUID(BLEUUID(OTA_SERVICE_UUID));
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    // Slow down advertising interval to save power (units of 0.625ms)
    // 0x0400 = 640ms interval (default ~100ms is too aggressive for battery)
    adv->setMinInterval(0x400);
    adv->setMaxInterval(0x800);
    BLEDevice::startAdvertising();

    LOG_INFO("BLE: advertising");
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

void ble_srv_send(const char *data)
{
    LOG_DEBUG("BLE send: '%s'", data);
    if (pTxChar) {
        size_t len = strlen(data);
        pTxChar->setValue((uint8_t *)data, len);
        pTxChar->notify();
    }
}

// ── Data service updates ──────────────────────────────────────
void ble_srv_update_steps(uint32_t steps)
{
    if (!pStepChar) return;
    ble_steps = steps;
    pStepChar->setValue(ble_steps);
    if (ble_is_connected()) {
        pStepChar->notify();
        LOG_VERBOSE("BLE data: steps=%lu", steps);
    }
}

void ble_srv_update_batt_raw(uint16_t mv)
{
    if (!pBattRawChar) return;
    ble_batt_raw = mv;
    pBattRawChar->setValue(ble_batt_raw);
    LOG_VERBOSE("BLE data: batt=%dmV", mv);
}

void ble_srv_update_activity(uint8_t act)
{
    if (!pActChar) return;
    ble_activity = act;
    pActChar->setValue(ble_activity);
    if (ble_is_connected()) {
        pActChar->notify();
        static const char *names[] = {"walk", "run", "idle"};
        LOG_DEBUG("BLE data: activity=%s", act < 3 ? names[act] : "?");
    }
}

// Send voice chat result via BLE: "voice:result|transcription|response"
void ble_srv_send_voice_result(const char *transcription, const char *response)
{
    if (!transcription || !response) return;
    char buf[512];
    snprintf(buf, sizeof(buf), "voice:result|%s|%s", transcription, response);
    ble_srv_send(buf);
}

bool ble_is_connected(void)
{
    return pServer && pServer->getConnectedCount() > 0;
}

void ble_srv_update_ota_state(uint8_t state, uint8_t progress)
{
    if (!pOtaStateChar) return;
    uint8_t val[2] = {state, progress};
    pOtaStateChar->setValue(val, 2);
    if (ble_is_connected()) {
        pOtaStateChar->notify();
    }
}

void* ble_srv_get_server(void)
{
    return pServer;
}

void ble_srv_set_dnd(bool enable)
{
    dnd_enabled = enable;
    LOG_INFO("BLE: DND %s", enable ? "ON" : "OFF");
}

bool ble_srv_get_dnd(void)
{
    return dnd_enabled;
}

void ble_srv_update_imu_features(const float *features, int count)
{
    if (!pImuFeatChar || !features || count <= 0) return;
    // Pack floats as raw bytes (max 48 bytes = 12 floats)
    int n = count > 12 ? 12 : count;
    pImuFeatChar->setValue((uint8_t *)features, n * sizeof(float));
    if (ble_is_connected()) {
        pImuFeatChar->notify();
    }
}
