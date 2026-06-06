#include <Arduino.h>
#include "pin_config.h"
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32SPI.h>
#include <display/Arduino_ST7789.h>
#include <lvgl.h>
#include "lv_port_disp.h"
#include <CST816S.h>
#include "lv_port_indev.h"
#include <Wire.h>
#include "SensorPCF85063.hpp"
#include "SensorQMI8658.hpp"
#include "XPowersLib.h"
#include "service/wifi_ntp.h"
#include "service/ble_srv.h"
#include "stopwatch.h"
#include "weather.h"
#include "activity.h"
#include "player.h"
#include "service/tf_card.h"
#include "service/audio.h"
#include "service/voice_chat.h"
#include "voice_chat_ui.h"
#include "service/ota_update.h"
#include "service/ble_hid.h"
#include "fall_detect.h"
#include "step_counter.h"
#include "wrist_detect.h"
#include "ui_pages.h"
#include "settings_page.h"
#include "nvs_store.h"
#include "motion_intensity.h"
#include "sleep_tracker.h"
#include "ui_styles.h"
#include "debug_log.h"
#include "sensor_task.h"
#include <math.h>
#include <esp_sleep.h>

// ── Hardware globals ──
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;
SensorPCF85063 rtc;
SensorQMI8658 imu;
XPowersPMU pmu;

// acc, gyr are now managed by sensor_task (mutex-protected)

// ── Page management ──
static int current_page = 0;
static const int NUM_PAGES = 11;
static lv_obj_t *pages[11];

// ── NTP sync ──
static unsigned long last_ntp_attempt = 0;
static bool ntp_synced = false;

// ── WiFi power management ──
static unsigned long wifi_off_time = 0;
static const unsigned long WIFI_ON_INTERVAL_MS = 600000;  // 10 min
static bool wifi_was_turned_off = false;

// ── Screen timeout ──
static unsigned long last_activity_time = 0;
static bool screen_on = true;
static const unsigned long DISPLAY_TIMEOUT_MS = 10000;
static const unsigned long DEEP_SLEEP_TIMEOUT_MS = 30000;

// ── Activity state ──
static int current_activity = -1;

// ── RTC helper for NTP sync ──
void set_rtc_from_tm(struct tm *ti) {
    rtc.setDateTime(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
        ti->tm_hour, ti->tm_min, ti->tm_sec);
    LOG_INFO("RTC: time updated from NTP");
}

// ── Battery helpers ──
static uint16_t read_batt_voltage_raw(void) {
    int h5 = pmu.readRegister(0x34);
    int l8 = pmu.readRegister(0x35);
    if (h5 < 0 || l8 < 0) return 0;
    return ((h5 & 0x1F) << 8) | l8;
}
static int read_batt_percent_raw(void) {
    return pmu.readRegister(0xA4);
}
static bool batt_is_valid(void) {
    uint16_t mv = read_batt_voltage_raw();
    return (mv >= 500 && mv <= 5000);
}

// ── Backlight / screen ──
static void set_backlight(bool on) {
    digitalWrite(LCD_BL, on ? HIGH : LOW);
    screen_on = on;
}

static void reset_activity_timer(void) {
    last_activity_time = millis();
    if (!screen_on) set_backlight(true);
}

// ── Page switching ──
static void switch_page(int dir) {
    int next = current_page + dir;
    if (next < 0 || next >= NUM_PAGES) return;
    current_page = next;
    lv_scr_load_anim(pages[next],
        dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        200, 0, false);
}

static void handle_gesture(void) {
    int g = touch->data.gestureID;
    if (g == SWIPE_LEFT) switch_page(1);
    else if (g == SWIPE_RIGHT) switch_page(-1);
    else if (g == SWIPE_UP) { set_backlight(true); reset_activity_timer(); }
    else if (g == SWIPE_DOWN) { set_backlight(false); }
}

// ── Build telemetry for UI ──
static void fill_telemetry(ui_telemetry_t *t) {
    RTC_DateTime dt = rtc.getDateTime();
    t->hour = dt.hour; t->minute = dt.minute; t->second = dt.second;
    t->day = dt.day; t->month = dt.month; t->year = dt.year; t->week = dt.week;
    t->batt_percent = read_batt_percent_raw();
    t->batt_mv = read_batt_voltage_raw();
    t->batt_valid = batt_is_valid();
    xpowers_chg_status_t cs = pmu.getChargerStatus();
    t->charging = (cs == XPOWERS_AXP2101_CHG_CC_STATE ||
                   cs == XPOWERS_AXP2101_CHG_PRE_STATE ||
                   cs == XPOWERS_AXP2101_CHG_TRI_STATE);
    t->usb_powered = pmu.isVbusIn();
    t->step_count = step_counter_get();
    t->wifi_connected = wifi_is_connected();
    // Read IMU data under mutex protection
    sensor_data_lock();
    t->acc_x = acc.x; t->acc_y = acc.y; t->acc_z = acc.z;
    t->gyr_x = gyr.x; t->gyr_y = gyr.y; t->gyr_z = gyr.z;
    sensor_data_unlock();
    t->intensity = motion_intensity_get();
    t->mets = motion_intensity_get_mets();
    t->calories = motion_intensity_get_calories();
    t->sleeping = sleep_tracker_is_sleeping();
    t->sleep_total_min = sleep_tracker_get_total_minutes();
    t->sleep_deep_min = sleep_tracker_get_deep_minutes();
}

// ═══════════════════════════════════════════════════════════════
// Setup
// ═══════════════════════════════════════════════════════════════
void setup() {
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    last_activity_time = millis();

    USBSerial.begin(115200);
    unsigned long start = millis();
    while (!USBSerial && millis() - start < 3000) delay(10);
    LOG_INFO("Booting... Firmware: %s", FIRMWARE_VERSION);

    Wire.begin(IIC_SDA, IIC_SCL);

    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
    gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);
    if (!gfx->begin()) { while (true) delay(100); }

    // Clear physical rows 0-19 and 304-319 (outside LVGL space)
    bus->beginWrite();
    bus->writeC8D16D16(ST7789_CASET, 0, 239);
    bus->writeC8D16D16(ST7789_RASET, 0, 19);
    bus->writeCommand(ST7789_RAMWR);
    bus->writeRepeat(0x0000, 240 * 20);
    bus->writeC8D16D16(ST7789_RASET, 304, 319);
    bus->writeCommand(ST7789_RAMWR);
    bus->writeRepeat(0x0000, 240 * 16);
    bus->endWrite();

    lv_init();
    lv_port_disp_init();
    ui_styles_init();
    tf_init();
    audio_init();
    voice_chat_init();
    fall_detect_init();
    step_counter_init();
    wrist_detect_init();
    motion_intensity_init();
    sleep_tracker_init();
    wrist_detect_set_callback(reset_activity_timer);

    touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);
    touch->begin();
    touch->disable_auto_sleep();  // Prevent touch controller from sleeping
    touch->enable_double_click(); // Enable double-tap gesture
    lv_port_indev_init();

    // Initialize UI pages
    ui_pages_init(pages, NUM_PAGES, touch);
    // Settings page (index 10)
    pages[10] = settings_page_create();

    if (rtc.init(Wire, IIC_SDA, IIC_SCL, PCF85063_SLAVE_ADDRESS)) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year < 2026) rtc.setDateTime(2026, 5, 20, 19, 0, 0);
    }

    if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
            SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0, true);
        imu.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS,
            SensorQMI8658::GYR_ODR_112_1Hz, SensorQMI8658::LPF_MODE_3, true);
        imu.enableGyroscope();
        imu.enableAccelerometer();
    }

    if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        pmu.disableDC2(); pmu.disableDC3(); pmu.disableDC4(); pmu.disableDC5();
        pmu.disableALDO2(); pmu.disableALDO3(); pmu.disableALDO4();
        pmu.disableBLDO1(); pmu.disableBLDO2();
        pmu.disableCPUSLDO(); pmu.disableDLDO1(); pmu.disableDLDO2();
        pmu.setDC1Voltage(3300); pmu.enableDC1();
        pmu.setALDO1Voltage(3300); pmu.enableALDO1();
        pmu.enableVbusVoltageMeasure(); pmu.enableBattVoltageMeasure();
        pmu.enableSystemVoltageMeasure();
        pmu.enableBattDetection();
        pmu.enableGauge();
        pmu.fuelGaugeControl(false, true);
        pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V35);
        pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
        pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_200MA);
        pmu.disableTSPinMeasure();
        pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ); pmu.clearIrqStatus();
        pmu.writeRegister(0x12, 0x01);
        int icc = pmu.readRegister(0x62);
        pmu.writeRegister(0x62, icc | 0x80);
        delay(200);
        LOG_INFO("PMU: connected=%d vbus_in=%d chg=%d",
            pmu.isBatteryConnect(), pmu.isVbusIn(), pmu.getChargerStatus());
    }

    // Start sensor reading task on Core 0 (125Hz, high priority)
    sensor_task_start(&imu);

    ble_srv_init();
    ble_hid_init(ble_srv_get_server());
    wifi_ntp_init();

    // NVS: restore step count and settings
    nvs_store_init();
    int saved_steps = nvs_get_steps_today();
    step_counter_set(saved_steps);
    LOG_INFO("Restored %d steps from NVS", saved_steps);

    LOG_INFO("Ready");
}

// ═══════════════════════════════════════════════════════════════
// Main loop
// ═══════════════════════════════════════════════════════════════
void loop() {
    lv_timer_handler();
    wifi_ntp_loop();
    // Handle OTA restart (download runs in background task)
    if (ota_check_restart()) {
        delay(100);
        ESP.restart();
    }

    // Report OTA state changes to BLE
    {
        static uint8_t last_ota_state = 255;
        ota_state_t os = ota_get_state();
        if ((uint8_t)os != last_ota_state) {
            last_ota_state = (uint8_t)os;
            ble_srv_update_ota_state((uint8_t)os, ota_get_progress());
        }
        if (os == OTA_WRITING || os == OTA_DOWNLOADING) {
            ble_srv_update_ota_state((uint8_t)os, ota_get_progress());
        }
    }

    if (wifi_is_connected() && !ntp_synced) ntp_synced = wifi_ntp_sync();
    if (ntp_synced && millis() - last_ntp_attempt > 3600000) {
        last_ntp_attempt = millis(); wifi_ntp_sync();
    }

    // WiFi power management
    if (wifi_is_powered()) {
        if (ntp_synced && wifi_off_time == 0) {
            wifi_off_time = millis();
        }
        if (wifi_off_time > 0 && millis() - wifi_off_time > 30000 && ota_get_state() == OTA_IDLE) {
            wifi_power_off();
            wifi_was_turned_off = true;
        }
    } else {
        if (wifi_was_turned_off && millis() - wifi_off_time > WIFI_ON_INTERVAL_MS) {
            wifi_power_on();
            wifi_off_time = 0;
        }
    }

    // Handle BLE notifications
    if (ble_notification.has_new) {
        ble_notification.has_new = 0;
        if (ble_srv_get_dnd()) {
            LOG_INFO("Notify: [DND] [%s] %s",
                ble_notification.app_id, ble_notification.title);
        } else {
            LOG_INFO("Notify: [%s] %s - %s",
                ble_notification.app_id, ble_notification.title, ble_notification.body);
        }
        char reply[64];
        snprintf(reply, sizeof(reply), "ack:%s", ble_notification.app_id);
        ble_srv_send(reply);
    }

    // Serial-to-BLE bridge + OTA command
    static char serial_buf[256];
    static int serial_len = 0;
    while (USBSerial.available() && serial_len < 255) {
        char c = USBSerial.read();
        if (c == '\n' || c == '\r') {
            if (serial_len > 0) {
                serial_buf[serial_len] = 0;
                if (strncmp(serial_buf, "ota ", 4) == 0) {
                    LOG_INFO("OTA: starting from %s", serial_buf + 4);
                    ota_start(serial_buf + 4);
                } else {
                    ble_srv_send(serial_buf);
                }
                serial_len = 0;
            }
        } else {
            serial_buf[serial_len++] = c;
        }
    }

    // IMU data is read by sensor task on Core 0
    // fall detection alert still checked here

    // Handle fall detection alert
    if (fall_detect_has_fallen()) {
        LOG_INFO("*** FALL DETECTED! Sending alert ***");
        set_backlight(true);
        ble_srv_send("ALERT:FALL_DETECTED");
        strncpy(ble_notification.app_id, "FALL", sizeof(ble_notification.app_id));
        strncpy(ble_notification.title, "Fall Detected!", sizeof(ble_notification.title));
        strncpy(ble_notification.body, "Press to dismiss", sizeof(ble_notification.body));
        ble_notification.has_new = 1;
        current_page = 3;
        lv_scr_load_anim(pages[3], LV_SCR_LOAD_ANIM_MOVE_TOP, 200, 0, false);
    }

    // Periodic UI updates (reduced rate when screen off)
    static unsigned long last_tick = 0;
    unsigned long tick_interval = screen_on ? 1000 : 5000;
    if (millis() - last_tick > tick_interval) {
        last_tick = millis();
        ui_telemetry_t telem;
        fill_telemetry(&telem);

        // Daily step counter reset check
        if (nvs_check_daily_reset(telem.day)) {
            step_counter_reset();
            motion_intensity_reset_calories();
            sleep_tracker_reset();
        }

        if (screen_on) {
            ui_update_watchface(&telem);
            if (current_page == 1) ui_update_analog(telem.hour, telem.minute, telem.second);
            if (current_page == 2) ui_update_sensor_page(&telem);
            if (current_page == 3) ui_update_notif_page();
            if (current_page == 5) weather_update();
            if (current_page == 6) activity_update();
            if (current_page == 7) player_update();
            if (current_page == 8) voice_chat_page_update();
            if (current_page == 10) settings_page_update();
        } else {
            ui_update_watchface(&telem);
        }

        // Sleep tracking (update every tick with intensity + time)
        sleep_tracker_update(telem.intensity, telem.hour, telem.minute);

        // Push telemetry to BLE
        int act = activity_get_current();
        if (act != current_activity) {
            current_activity = act;
            ble_srv_update_activity(act >= 0 ? (uint8_t)act : 2);
        }
        ble_srv_update_steps(step_counter_get());

        // Periodically save steps to NVS (every 60s to avoid excessive writes)
        {
            static unsigned long last_nvs_save = 0;
            if (millis() - last_nvs_save > 60000) {
                last_nvs_save = millis();
                nvs_set_steps_today(step_counter_get());
            }
        }
        if (batt_is_valid())
            ble_srv_update_batt_raw(read_batt_voltage_raw());
        else if (pmu.isVbusIn())
            ble_srv_update_batt_raw(0xFFFF);

        // Push IMU features for AI co-inference (every 5s)
        {
            static unsigned long last_feat_time = 0;
            if (millis() - last_feat_time > 5000) {
                last_feat_time = millis();
                const float *feat = activity_get_features();
                if (feat) ble_srv_update_imu_features(feat, 12);
            }
        }
    }

    // Stopwatch needs sub-second precision
    if (current_page == 4) stopwatch_update();

    // Screen timeout
    if (screen_on && millis() - last_activity_time > DISPLAY_TIMEOUT_MS) {
        set_backlight(false);
    }

    // Deep sleep timeout
    if (!screen_on && millis() - last_activity_time > DEEP_SLEEP_TIMEOUT_MS) {
        if (pmu.isVbusIn()) {
            last_activity_time = millis();
        } else {
            LOG_INFO("Entering deep sleep...");
            delay(100);
            pmu.disableDC1();
            pmu.disableALDO1();
            pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
            esp_sleep_enable_timer_wakeup(60000000);
            esp_sleep_enable_ext0_wakeup((gpio_num_t)TP_INT, 0);
            esp_deep_sleep_start();
        }
    }

    // Gesture handling
    if (touch) {
        static uint8_t last_gesture = 0xFF;
        static bool last_pressed = false;
        uint8_t g = touch->data.gestureID;
        bool pressed = (touch->data.event == 0) &&
                       (touch->data.x > 0 || touch->data.y > 0);

        if (pressed && !last_pressed) {
            reset_activity_timer();
        }
        last_pressed = pressed;

        if (g != last_gesture && g != NONE) {
            last_gesture = g;
            handle_gesture();
        }
        // Reset last_gesture when gesture ends
        if (g == NONE) last_gesture = 0xFF;
    }

    delay(2);
}
