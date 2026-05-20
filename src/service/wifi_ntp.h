#ifndef WIFI_NTP_H
#define WIFI_NTP_H

#include <Arduino.h>

#define WIFI_SSID "iQOO"
#define WIFI_PASS "12345678"
#define NTP_SERVER "pool.ntp.org"
#define TZ_OFFSET 28800

void wifi_ntp_init(void);
bool wifi_ntp_sync(void);
bool wifi_is_connected(void);
void wifi_ntp_set_creds(const char *ssid, const char *pass);
void wifi_ntp_loop(void);

#endif
