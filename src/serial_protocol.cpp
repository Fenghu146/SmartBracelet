#include "serial_protocol.h"
#include <ArduinoJson.h>
#include "debug_log.h"
#include "service/ota_update.h"
#include "nvs_store.h"
#include "notif_history.h"
#include "service/voice_chat.h"

static char notif_app[16] = {0};
static char notif_title[64] = {0};
static char notif_body[128] = {0};
static bool has_new_notif = false;

void serial_push_telemetry(const ui_telemetry_t *t) {
    JsonDocument doc;
    doc["e"] = "t";
    doc["b"] = t->batt_percent;
    doc["mv"] = t->batt_mv;
    doc["chg"] = t->charging ? 1 : 0;
    doc["usb"] = t->usb_powered ? 1 : 0;
    doc["st"] = t->step_count;
    doc["wifi"] = t->wifi_connected ? 1 : 0;
    JsonArray acc = doc["acc"].to<JsonArray>();
    acc.add(t->acc_x); acc.add(t->acc_y); acc.add(t->acc_z);
    JsonArray gyr = doc["gyr"].to<JsonArray>();
    gyr.add(t->gyr_x); gyr.add(t->gyr_y); gyr.add(t->gyr_z);
    doc["int"] = t->intensity;
    doc["met"] = t->mets;
    doc["cal"] = t->calories;
    serializeJson(doc, USBSerial);
    USBSerial.println();
}

void serial_push_event(const char *type, const char *msg) {
    JsonDocument doc;
    doc["e"] = type;
    doc["msg"] = msg;
    serializeJson(doc, USBSerial);
    USBSerial.println();
}

static void dispatch_command(const char *cmd, JsonDocument &doc) {
    if (strcmp(cmd, "notify") == 0) {
        const char *app = doc["app"];
        const char *title = doc["title"];
        const char *body = doc["body"];
        if (app && title) {
            strncpy(notif_app, app, sizeof(notif_app) - 1);
            strncpy(notif_title, title, sizeof(notif_title) - 1);
            if (body) strncpy(notif_body, body, sizeof(notif_body) - 1);
            else notif_body[0] = '\0';
            notif_app[sizeof(notif_app) - 1] = '\0';
            notif_title[sizeof(notif_title) - 1] = '\0';
            notif_body[sizeof(notif_body) - 1] = '\0';
            has_new_notif = true;
            LOG_INFO("Serial notify: [%s] %s - %s", notif_app, notif_title, notif_body);
        }
    } else if (strcmp(cmd, "time") == 0) {
        unsigned long epoch = doc["epoch"];
        if (epoch > 100000) {
            struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            LOG_INFO("Serial time: set to %lu", epoch);
        }
    } else if (strcmp(cmd, "ota") == 0) {
        const char *url = doc["url"];
        if (url) {
            LOG_INFO("Serial OTA: starting from %s", url);
            ota_start(url);
        }
    } else if (strcmp(cmd, "dnd") == 0) {
        int on = doc["on"] | 0;
        nvs_set_dnd(on != 0);
        LOG_INFO("Serial DND: %s", on ? "ON" : "OFF");
    } else if (strcmp(cmd, "loc") == 0) {
        const char *lat = doc["lat"];
        const char *lon = doc["lon"];
        if (lat) nvs_set_weather_lat(lat);
        if (lon) nvs_set_weather_lon(lon);
        LOG_INFO("Serial loc: lat=%s lon=%s", lat ? lat : "?", lon ? lon : "?");
    } else if (strcmp(cmd, "voice") == 0) {
        const char *vc = doc["vc"];
        const char *arg = doc["arg"];
        if (vc) {
            extern void voice_chat_on_command(const char *cmd, const char *arg);
            voice_chat_on_command(vc, arg ? arg : "");
        }
    } else {
        LOG_DEBUG("Serial unknown cmd: %s", cmd);
    }
}

void serial_protocol_process(void) {
    static char buf[512];
    static int pos = 0;
    while (USBSerial.available()) {
        char c = USBSerial.read();
        if (c == '\n' || c == '\r') {
            if (pos == 0) continue;
            buf[pos] = '\0';
            pos = 0;
            if (buf[0] != '{') continue;
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, buf);
            if (err) {
                LOG_DEBUG("Serial JSON err: %s", err.c_str());
                continue;
            }
            const char *cmd = doc["c"];
            if (cmd) dispatch_command(cmd, doc);
        } else {
            if (pos < (int)sizeof(buf) - 1) buf[pos++] = c;
        }
    }
}

bool serial_notification_has_new(void) { return has_new_notif; }
const char* serial_notification_app(void) { return notif_app; }
const char* serial_notification_title(void) { return notif_title; }
const char* serial_notification_body(void) { return notif_body; }
void serial_notification_consume(void) { has_new_notif = false; }
