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
#include "ui_styles.h"
#include "debug_log.h"
#include "sensor_task.h"
#include "backlight.h"
#include "watch_faces.h"
#include "notif_history.h"
#include "batt_health.h"
#include "quick_panel.h"
#include <math.h>
#include <esp_sleep.h>
#include <esp_system.h>

// 鈹€鈹€ Hardware globals 鈹€鈹€
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;
SensorPCF85063 rtc;
SensorQMI8658 imu;
XPowersPMU pmu;

// acc, gyr are now managed by sensor_task (mutex-protected)

// 鈹€鈹€ Page management 鈹€鈹€
static int current_page = 0;
static lv_obj_t *pages[PAGE_COUNT];

// 鈹€鈹€ NTP sync 鈹€鈹€
static unsigned long last_ntp_attempt = 0;
static bool ntp_synced = false;

// 鈹€鈹€ WiFi power management 鈹€鈹€
static unsigned long wifi_off_time = 0;
static const unsigned long WIFI_ON_INTERVAL_MS = 600000;  // 10 min
static bool wifi_was_turned_off = false;

// 鈹€鈹€ Screen timeout 鈹€鈹€
static unsigned long last_activity_time = 0;
static const unsigned long DISPLAY_TIMEOUT_MS = 10000;
static const unsigned long DEEP_SLEEP_TIMEOUT_MS = 30000;

// 鈹€鈹€ Activity state 鈹€鈹€
static int current_activity = -1;

// -- Watch face --
static int current_face = 0;
static lv_obj_t *sport_page = nullptr;

// -- Named constants (replacing magic numbers) --
#define BAUD_RATE               115200
#define USB_CONNECT_DELAY_MS    500
#define DISPLAY_TEST_BLUE       0x001F
#define LCD_CLEAR_TOP_END       19
#define LCD_CLEAR_BOT_START     304
#define LCD_CLEAR_BOT_END       319
#define LCD_CLEAR_WIDTH         240
#define TICK_INTERVAL_ON_MS     1000
#define TICK_INTERVAL_OFF_MS    5000
#define NVS_SAVE_INTERVAL_MS    60000
#define IMU_FEATURE_INTERVAL_MS 5000
#define NTP_RESYNC_INTERVAL_MS  3600000
#define WIFI_POWER_OFF_DELAY_MS 30000
#define PMU_SETTLE_DELAY_MS     200
#define FALL_NOTIFY_PAGE        PAGE_NOTIF
#define FALL_PAGE_ANIM_MS        200
#define DEFAULT_YEAR            2026

// 鈹€鈹€ RTC helper for NTP sync 鈹€鈹€
void set_rtc_from_tm(struct tm *ti) {
    rtc.setDateTime(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
        ti->tm_hour, ti->tm_min, ti->tm_sec);
}

// 鈹€鈹€ Battery helpers 鈹€鈹€
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
static bool is_charging(xpowers_chg_status_t cs) {
    return (cs == XPOWERS_AXP2101_CHG_CC_STATE ||
            cs == XPOWERS_AXP2101_CHG_PRE_STATE ||
            cs == XPOWERS_AXP2101_CHG_TRI_STATE);
}


static void reset_activity_timer(void) {
    last_activity_time = millis();
    if (!screen_is_on()) set_backlight(true);
}

// 鈹€鈹€ Page switching 鈹€鈹€
static void switch_page(int dir) {
    int next = current_page + dir;
    if (next < 0 || next >= PAGE_COUNT) return;
    current_page = next;
    nvs_set_crash_page(current_page);  // Save for crash recovery
    lv_scr_load_anim(pages[next],
        dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        200, 0, false);
}

static void handle_gesture(void) {
    int g = touch->data.gestureID;
    if (g == SWIPE_LEFT) switch_page(1);
    else if (g == SWIPE_RIGHT) switch_page(-1);
    else if (g == SWIPE_UP) { set_backlight(true); reset_activity_timer(); }
    else if (g == SWIPE_DOWN) { if (quick_panel_is_visible()) quick_panel_hide(); else quick_panel_show(); }
    else if (g == LONG_PRESS) {
        // Cycle watch face
        current_face = watch_face_next(current_face);
        nvs_set_watch_face(current_face);
        // Load the appropriate face
        if (current_face == FACE_ANALOG) {
            lv_scr_load(pages[PAGE_ANALOG]);
        } else if (current_face == FACE_SPORT) {
            lv_scr_load(sport_page);
        } else {
            lv_scr_load(pages[PAGE_DIGITAL]);
        }
        LOG_INFO("Watch face: %s", watch_face_name(current_face));
    }
}

// 鈹€鈹€ Build telemetry for UI 鈹€鈹€
static void fill_telemetry(ui_telemetry_t *t) {
    RTC_DateTime dt = rtc.getDateTime();
    t->hour = dt.hour; t->minute = dt.minute; t->second = dt.second;
    t->day = dt.day; t->month = dt.month; t->year = dt.year; t->week = dt.week;
    t->batt_mv = read_batt_voltage_raw();
    t->batt_valid = (t->batt_mv >= 500 && t->batt_mv <= 5000);
    t->batt_percent = read_batt_percent_raw();
    xpowers_chg_status_t cs = pmu.getChargerStatus();
    t->charging = is_charging(cs);
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
}

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲锟?
// Setup
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲锟?
// -- Setup: display hardware init --
static void setup_display(void) {
    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
    gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);
    if (!gfx->begin()) {
        for (int i = 0; i < 10; i++) {
            digitalWrite(LCD_BL, (i % 2) ? HIGH : LOW);
            delay(200);
        }
        digitalWrite(LCD_BL, HIGH);
        while (true) delay(100);
    }

    gfx->fillScreen(DISPLAY_TEST_BLUE);
    delay(100);

    // Clear physical rows 0-19 and 304-319 (outside LVGL space)
    bus->beginWrite();
    bus->writeC8D16D16(ST7789_CASET, 0, 239);
    bus->writeC8D16D16(ST7789_RASET, 0, LCD_CLEAR_TOP_END);
    bus->writeCommand(ST7789_RAMWR);
    bus->writeRepeat(0x0000, LCD_CLEAR_WIDTH * 20);
    bus->writeC8D16D16(ST7789_RASET, LCD_CLEAR_BOT_START, LCD_CLEAR_BOT_END);
    bus->writeCommand(ST7789_RAMWR);
    bus->writeRepeat(0x0000, LCD_CLEAR_WIDTH * 16);
    bus->endWrite();
}

// -- Setup: software module init --
static void setup_modules(void) {
    tf_init();
    audio_init();
    voice_chat_init();
    fall_detect_init();
    step_counter_init();
    wrist_detect_init();
    motion_intensity_init();
    notif_history_init();
    batt_health_init();
    quick_panel_init();
    wrist_detect_set_callback(reset_activity_timer);
}

// -- Setup: touch controller --
static void setup_touch(void) {
    touch = new CST816S(TP_SDA, TP_SCL, TP_RST, TP_INT);
    touch->begin();
    touch->disable_auto_sleep();
    touch->enable_double_click();
    lv_port_indev_init();

    ui_pages_init(pages, PAGE_COUNT, touch);
    pages[PAGE_SETTINGS] = settings_page_create();

    sport_page = lv_obj_create(NULL);
    sport_face_create(sport_page);

    current_face = nvs_get_watch_face();
    if (current_face >= FACE_COUNT) current_face = 0;
}

// -- Setup: RTC, IMU, PMU, sensors, BLE, WiFi, NVS --
static void setup_system(void) {
    if (rtc.init(Wire, IIC_SDA, IIC_SCL, PCF85063_SLAVE_ADDRESS)) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year < DEFAULT_YEAR) rtc.setDateTime(DEFAULT_YEAR, 5, 20, 19, 0, 0);
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
        delay(PMU_SETTLE_DELAY_MS);
        LOG_INFO("PMU: connected=%d vbus_in=%d chg=%d",
            pmu.isBatteryConnect(), pmu.isVbusIn(), pmu.getChargerStatus());
    }

    sensor_task_start(&imu);

    ble_srv_init();
    wifi_ntp_init();

    nvs_store_init();
    int saved_steps = nvs_get_steps_today();
    step_counter_set(saved_steps);
    LOG_INFO("Restored %d steps from NVS", saved_steps);

    // Crash recovery
    esp_reset_reason_t reset_reason = esp_reset_reason();
    LOG_INFO("Reset reason: %d", reset_reason);
    if (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_WDT ||
        reset_reason == ESP_RST_INT_WDT || reset_reason == ESP_RST_TASK_WDT) {
        int crash_page = nvs_get_crash_page();
        int crash_steps = nvs_get_crash_steps();
        if (crash_page >= 0 && crash_page < PAGE_COUNT) {
            current_page = crash_page;
            lv_scr_load(pages[crash_page]);
            LOG_INFO("Crash recovery: restored page %d, steps %d", crash_page, crash_steps);
        }
        if (crash_steps > 0 && saved_steps == 0) {
            step_counter_set(crash_steps);
            LOG_INFO("Crash recovery: restored %d steps from crash state", crash_steps);
        }
    }
}

void setup() {
    backlight_init();
    last_activity_time = millis();

    USBSerial.begin(BAUD_RATE);
    delay(USB_CONNECT_DELAY_MS);
    LOG_INFO("Booting... Firmware: %s", FIRMWARE_VERSION);

    Wire.begin(IIC_SDA, IIC_SCL);

    setup_display();
    lv_init();
    lv_port_disp_init();
    ui_styles_init();
    setup_modules();
    setup_touch();
    setup_system();

    LOG_INFO("Ready");
}


// -- Sub-loop: USBSerial-to-BLE bridge + OTA command --
static void loop_serial_bridge(void) {
    static char serial_buf[256];
    static int serial_len = 0;
    while (USBSerial.available()) {
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
            if (serial_len < 255) {
                serial_buf[serial_len++] = c;
            } else {
                // Buffer overflow: discard and reset
                serial_len = 0;
            }
        }
    }
}

// -- Sub-loop: BLE notifications, OTA, NTP, WiFi power --
static void loop_communication(void) {
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
    if (ntp_synced && millis() - last_ntp_attempt > NTP_RESYNC_INTERVAL_MS) {
        last_ntp_attempt = millis(); wifi_ntp_sync();
    }

    // WiFi power management
    if (wifi_is_powered()) {
        if (ntp_synced && wifi_off_time == 0) wifi_off_time = millis();
        if (wifi_off_time > 0 && millis() - wifi_off_time > WIFI_POWER_OFF_DELAY_MS && ota_get_state() == OTA_IDLE) {
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
        RTC_DateTime dt = rtc.getDateTime();
        notif_history_add(ble_notification.app_id, ble_notification.title,
                         ble_notification.body, dt.hour, dt.minute);
        if (ble_srv_get_dnd()) {
            LOG_INFO("Notify: [DND] [%s] %s", ble_notification.app_id, ble_notification.title);
        } else {
            LOG_INFO("Notify: [%s] %s - %s", ble_notification.app_id, ble_notification.title, ble_notification.body);
        }
        char reply[64];
        snprintf(reply, sizeof(reply), "ack:%s", ble_notification.app_id);
        ble_srv_send(reply);
    }

    loop_serial_bridge();
}

// -- Sub-loop: periodic telemetry, UI updates, BLE push --
// -- Fall detection alert handler --
static void handle_fall_alert(void) {
    if (fall_detect_has_fallen()) {
        LOG_INFO("*** FALL DETECTED! Sending alert ***");
        set_backlight(true);
        ble_srv_send("ALERT:FALL_DETECTED");
        strncpy(ble_notification.app_id, "FALL", sizeof(ble_notification.app_id));
        ble_notification.app_id[sizeof(ble_notification.app_id) - 1] = '\0';
        strncpy(ble_notification.title, "Fall Detected!", sizeof(ble_notification.title));
        ble_notification.title[sizeof(ble_notification.title) - 1] = '\0';
        strncpy(ble_notification.body, "Press to dismiss", sizeof(ble_notification.body));
        ble_notification.body[sizeof(ble_notification.body) - 1] = '\0';
        ble_notification.has_new = 1;
        current_page = FALL_NOTIFY_PAGE;
        lv_scr_load_anim(pages[FALL_NOTIFY_PAGE], LV_SCR_LOAD_ANIM_MOVE_TOP, FALL_PAGE_ANIM_MS, 0, false);
    }
}

// -- UI page update dispatcher --
static void update_ui_pages(const ui_telemetry_t *telem) {
    if (screen_is_on()) {
        ui_update_watchface(telem);
        if (current_page == PAGE_ANALOG) ui_update_analog(telem->hour, telem->minute, telem->second);
        if (current_page == PAGE_SENSOR) ui_update_sensor_page(telem);
        if (current_page == PAGE_NOTIF) ui_update_notif_page();
        if (current_page == PAGE_WEATHER) weather_update();
        if (current_page == PAGE_ACTIVITY) activity_update();
        if (current_page == PAGE_PLAYER) player_update();
        if (current_page == PAGE_VOICE) voice_chat_page_update();
        if (current_page == PAGE_SETTINGS) settings_page_update();
        if (current_face == FACE_SPORT && current_page == PAGE_DIGITAL) sport_face_update(telem);
        if (quick_panel_is_visible())
            quick_panel_update(telem->hour, telem->minute, telem->batt_percent, telem->wifi_connected, ble_is_connected());
    } else {
        ui_update_watchface(telem);
    }
}

// -- BLE telemetry push + NVS save + battery health --
static void push_ble_telemetry(const ui_telemetry_t *telem) {
    int act = activity_get_current();
    if (act != current_activity) {
        current_activity = act;
        ble_srv_update_activity(act >= 0 ? (uint8_t)act : 2);
    }
    ble_srv_update_steps(step_counter_get());

    // Periodically save steps to NVS (every 60s)
    {
        static unsigned long last_nvs_save = 0;
        if (millis() - last_nvs_save > NVS_SAVE_INTERVAL_MS) {
            last_nvs_save = millis();
            nvs_set_steps_today(step_counter_get());
            nvs_set_crash_page(current_page);
            nvs_set_crash_steps(step_counter_get());
        }
    }

    // Use cached values from fill_telemetry instead of re-reading PMU registers
    if (telem->batt_valid) ble_srv_update_batt_raw(telem->batt_mv);
    else if (telem->usb_powered) ble_srv_update_batt_raw(0xFFFF);

    // Battery health tracking (reuse cached charging state & voltage)
    batt_health_update(telem->charging, telem->batt_mv);

    // Push IMU features for AI co-inference (every 5s)
    {
        static unsigned long last_feat_time = 0;
        if (millis() - last_feat_time > IMU_FEATURE_INTERVAL_MS) {
            last_feat_time = millis();
            const float *feat = activity_get_features();
            if (feat) ble_srv_update_imu_features(feat, 12);
        }
    }
}

static void loop_telemetry(void) {
    handle_fall_alert();

    static unsigned long last_tick = 0;
    unsigned long tick_interval = screen_is_on() ? TICK_INTERVAL_ON_MS : TICK_INTERVAL_OFF_MS;
    if (millis() - last_tick > tick_interval) {
        last_tick = millis();
        ui_telemetry_t telem;
        fill_telemetry(&telem);

        if (nvs_check_daily_reset(telem.day)) {
            step_counter_reset();
            motion_intensity_reset_calories();
        }

        update_ui_pages(&telem);
        push_ble_telemetry(&telem);
    }

    if (current_page == PAGE_STOPWATCH) stopwatch_update();
}

// -- Sub-loop: screen timeout and deep sleep --
static void loop_power_management(void) {
    if (screen_is_on() && millis() - last_activity_time > DISPLAY_TIMEOUT_MS) {
        set_backlight(false);
    }

    if (!screen_is_on() && millis() - last_activity_time > DEEP_SLEEP_TIMEOUT_MS) {
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
}

// -- Sub-loop: touch gesture handling --
static void loop_gesture(void) {
    if (touch) {
        static uint8_t last_gesture = 0xFF;
        static bool last_pressed = false;
        uint8_t g = touch->data.gestureID;
        bool pressed = (touch->data.event == 0) && (touch->data.x > 0 || touch->data.y > 0);

        if (pressed && !last_pressed) reset_activity_timer();
        last_pressed = pressed;

        if (g != last_gesture && g != NONE) {
            last_gesture = g;
            handle_gesture();
        }
        if (g == NONE) last_gesture = 0xFF;
    }
}

// ===========================================================
// Main loop
// ===========================================================
void loop() {
    lv_timer_handler();
    wifi_ntp_loop();
    loop_communication();
    loop_telemetry();
    loop_power_management();
    loop_gesture();
    delay(2);
}
