#include "wifi_ntp.h"
#include "pin_config.h"
#include <WiFi.h>
#include <time.h>

static const char *wifi_ssid = WIFI_SSID;
static const char *wifi_pass = WIFI_PASS;
static bool connected = false;
static unsigned long last_attempt = 0;
static bool wifi_enabled = false;
static bool wifi_powered = true;  // WiFi radio on/off state

extern void rtc_set_from_epoch(time_t epoch);

void wifi_ntp_set_creds(const char *ssid, const char *pass)
{
    wifi_ssid = ssid;
    wifi_pass = pass;
}

void wifi_ntp_init(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    wifi_enabled = true;
    USBSerial.printf("WiFi: connecting to %s...\n", wifi_ssid);
    WiFi.begin(wifi_ssid, wifi_pass);
    last_attempt = millis();
}

bool wifi_is_connected(void)
{
    return connected && WiFi.status() == WL_CONNECTED;
}

void wifi_ntp_loop(void)
{
    if (!wifi_enabled) return;

    if (WiFi.status() == WL_CONNECTED) {
        if (!connected) {
            connected = true;
            USBSerial.printf("WiFi: connected, IP=%s\n",
                WiFi.localIP().toString().c_str());
        }
        return;
    }

    if (connected) {
        connected = false;
        USBSerial.println("WiFi: disconnected");
    }

    if (millis() - last_attempt > 30000) {
        last_attempt = millis();
        USBSerial.println("WiFi: retrying...");
        WiFi.begin(wifi_ssid, wifi_pass);
    }
}

bool wifi_ntp_sync(void)
{
    if (!wifi_is_connected()) {
        USBSerial.println("NTP: WiFi not connected");
        return false;
    }

    configTime(TZ_OFFSET, 0, NTP_SERVER);
    USBSerial.println("NTP: syncing...");

    time_t now = 0;
    for (int i = 0; i < 20; i++) {
        delay(500);
        time(&now);
        if (now > 100000) break;
    }

    if (now > 100000) {
        struct tm *ti = localtime(&now);
        USBSerial.printf("NTP: %04d-%02d-%02d %02d:%02d:%02d\n",
            ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
            ti->tm_hour, ti->tm_min, ti->tm_sec);

        extern void set_rtc_from_tm(struct tm *ti);
        set_rtc_from_tm(ti);
        return true;
    }

    USBSerial.println("NTP: timeout");
    return false;
}

void wifi_power_off(void) {
    if (!wifi_powered) return;
    wifi_powered = false;
    // Avoid a full WIFI_OFF / esp_wifi_deinit: on ESP32-S3 it can stall the
    // RF coexistence controller and tear down an in-flight BLE GATT link
    // (seen as "E wifi: timeout when WiFi un-init, type=4" right before a
    // Web Bluetooth disconnect). disconnect() without `true` keeps the driver
    // resident and lets BLE hold the radio, which is enough to drop STA power.
    WiFi.disconnect();
    connected = false;
    USBSerial.println("WiFi: STA disconnected (save power)");
}

void wifi_power_on(void) {
    if (wifi_powered) return;
    wifi_powered = true;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    WiFi.begin(wifi_ssid, wifi_pass);
    last_attempt = millis();
    USBSerial.printf("WiFi: radio on, connecting to %s...\n", wifi_ssid);
}

bool wifi_is_powered(void) {
    return wifi_powered;
}

int wifi_get_rssi(void) {
    if (!wifi_is_connected()) return 0;
    return WiFi.RSSI();
}
