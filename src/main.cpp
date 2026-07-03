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
#include "stopwatch.h"
#include "weather.h"
#include "service/audio.h"
#include "service/voice_chat.h"
#include "service/voice_assistant.h"
#include "voice_chat_ui.h"
#include "service/ota_update.h"
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
#include "serial_protocol.h"
#include <math.h>
#include <esp_system.h>

// -- BOOT button (GPIO0, active LOW) --
#define BOOT_BTN 0
static volatile bool boot_btn_pressed = false;
static unsigned long boot_press_time = 0;

static void IRAM_ATTR boot_btn_isr(void *arg) {
    boot_btn_pressed = true;
}

// -- Hardware globals --
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
CST816S *touch = nullptr;
SensorPCF85063 rtc;
SensorQMI8658 imu;
XPowersPMU pmu;

// -- Page management --
static int current_page = 0;
static lv_obj_t *pages[PAGE_COUNT];

// -- NTP sync --
static unsigned long last_ntp_attempt = 0;
static bool ntp_synced = false;

// -- WiFi power management --
static unsigned long wifi_off_time = 0;
static const unsigned long WIFI_ON_INTERVAL_MS = 600000;
static bool wifi_was_turned_off = false;

// -- Screen timeout --
static unsigned long last_activity_time = 0;
static const unsigned long DISPLAY_TIMEOUT_MS = 60000;

// -- Watch face --
static int current_face = 0;
static lv_obj_t *sport_page = nullptr;

// -- Named constants --
#define BAUD_RATE               115200
#define LCD_CLEAR_TOP_END       19
#define LCD_CLEAR_BOT_START     304
#define LCD_CLEAR_BOT_END       319
#define LCD_CLEAR_WIDTH         240
#define TICK_INTERVAL_ON_MS     1000
#define TICK_INTERVAL_OFF_MS    5000
#define NVS_SAVE_INTERVAL_MS    60000
#define NTP_RESYNC_INTERVAL_MS  3600000
#define WIFI_POWER_OFF_DELAY_MS 30000
#define PMU_SETTLE_DELAY_MS     200
#define FALL_NOTIFY_PAGE        PAGE_NOTIF
#define FALL_PAGE_ANIM_MS        200
#define DEFAULT_YEAR            2026

// -- RTC helper for NTP sync --
void set_rtc_from_tm(struct tm *ti) {
    rtc.setDateTime(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
        ti->tm_hour, ti->tm_min, ti->tm_sec);
}

// -- Battery helpers --
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

// -- Page switching --
static void switch_page(int dir) {
    int next = current_page + dir;
    if (next < 0 || next >= PAGE_COUNT) return;
    current_page = next;
    nvs_set_crash_page(current_page);
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
        current_face = watch_face_next(current_face);
        nvs_set_watch_face(current_face);
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

// -- Build telemetry for UI --
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
    sensor_data_lock();
    t->acc_x = acc.x; t->acc_y = acc.y; t->acc_z = acc.z;
    t->gyr_x = gyr.x; t->gyr_y = gyr.y; t->gyr_z = gyr.z;
    sensor_data_unlock();
    t->intensity = motion_intensity_get();
    t->mets = motion_intensity_get_mets();
    t->calories = motion_intensity_get_calories();
}

// Setup
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
    audio_init();
    voice_chat_init();
    va_init();
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

// -- Setup: RTC, IMU, PMU, sensors, WiFi, NVS --
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
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
        pmu.writeRegister(0x12, 0x01);
        int icc = pmu.readRegister(0x62);
        pmu.writeRegister(0x62, icc | 0x80);
        delay(PMU_SETTLE_DELAY_MS);
        LOG_INFO("PMU: connected=%d vbus_in=%d chg=%d",
            pmu.isBatteryConnect(), pmu.isVbusIn(), pmu.getChargerStatus());
    }

    sensor_task_start(&imu);

    wifi_ntp_init();

    nvs_store_init();
    int saved_steps = nvs_get_steps_today();
    step_counter_set(saved_steps);
    LOG_INFO("Restored %d steps from NVS", saved_steps);

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
    LOG_INFO("Booting... Firmware: %s", FIRMWARE_VERSION);

    Wire.begin(IIC_SDA, IIC_SCL);

    setup_display();
    lv_init();
    lv_port_disp_init();
    ui_styles_init();
    setup_touch();
    setup_modules();
    setup_system();

    pinMode(BOOT_BTN, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(BOOT_BTN), boot_btn_isr, NULL, FALLING);

    LOG_INFO("Ready");
}

// -- Sub-loop: serial protocol command processing (PC → watch) --
static void loop_serial_commands(void) {
    serial_protocol_process();
}

// -- Sub-loop: notifications from serial, OTA, NTP, WiFi power --
static void loop_communication(void) {
    if (ota_check_restart()) {
        delay(100);
        ESP.restart();
    }

    if (wifi_is_connected() && !ntp_synced) ntp_synced = wifi_ntp_sync();
    if (ntp_synced && millis() - last_ntp_attempt > NTP_RESYNC_INTERVAL_MS) {
        last_ntp_attempt = millis(); wifi_ntp_sync();
    }

    // WiFi power management (no BLE co-existence check needed)
    if (wifi_is_powered()) {
        if (ntp_synced && wifi_off_time == 0) wifi_off_time = millis();
        if (wifi_off_time > 0
            && millis() - wifi_off_time > WIFI_POWER_OFF_DELAY_MS
            && ota_get_state() == OTA_IDLE) {
            wifi_power_off();
            wifi_was_turned_off = true;
        }
    } else {
        if (wifi_was_turned_off && millis() - wifi_off_time > WIFI_ON_INTERVAL_MS) {
            wifi_power_on();
            wifi_off_time = 0;
        }
    }

    // Handle serial notifications
    if (serial_notification_has_new()) {
        RTC_DateTime dt = rtc.getDateTime();
        notif_history_add(serial_notification_app(), serial_notification_title(),
                         serial_notification_body(), dt.hour, dt.minute);
        if (nvs_get_dnd()) {
            LOG_INFO("Notify: [DND] [%s] %s", serial_notification_app(), serial_notification_title());
        } else {
            LOG_INFO("Notify: [%s] %s - %s", serial_notification_app(), serial_notification_title(), serial_notification_body());
        }
        serial_notification_consume();
    }
}

// -- Sub-loop: periodic telemetry, UI updates, serial push --
static void handle_fall_alert(void) {
    if (fall_detect_has_fallen()) {
        LOG_INFO("*** FALL DETECTED! Sending alert ***");
        set_backlight(true);
        serial_push_event("a", "FALL_DETECTED");
        notif_history_add("FALL", "Fall Detected!", "Press to dismiss",
                         rtc.getDateTime().hour, rtc.getDateTime().minute);
        current_page = FALL_NOTIFY_PAGE;
        lv_scr_load_anim(pages[FALL_NOTIFY_PAGE], LV_SCR_LOAD_ANIM_MOVE_TOP, FALL_PAGE_ANIM_MS, 0, false);
    }
}

static void update_ui_pages(const ui_telemetry_t *telem) {
    if (screen_is_on()) {
        ui_update_watchface(telem);
        if (current_page == PAGE_ANALOG) ui_update_analog(telem->hour, telem->minute, telem->second);
        if (current_page == PAGE_SENSOR) ui_update_sensor_page(telem);
        if (current_page == PAGE_NOTIF) ui_update_notif_page();
        if (current_page == PAGE_WEATHER) weather_update();
        if (current_page == PAGE_VOICE) voice_chat_page_update();
        if (current_page == PAGE_SETTINGS) settings_page_update();
        if (current_face == FACE_SPORT && current_page == PAGE_DIGITAL) sport_face_update(telem);
        if (quick_panel_is_visible())
            quick_panel_update(telem->hour, telem->minute, telem->batt_percent, telem->wifi_connected, false);
    } else {
        ui_update_watchface(telem);
    }
}

// -- Serial telemetry push + NVS save + battery health --
static void push_serial_telemetry(const ui_telemetry_t *telem) {
    serial_push_telemetry(telem);

    {
        static unsigned long last_nvs_save = 0;
        if (millis() - last_nvs_save > NVS_SAVE_INTERVAL_MS) {
            last_nvs_save = millis();
            nvs_set_steps_today(step_counter_get());
            nvs_set_crash_page(current_page);
            nvs_set_crash_steps(step_counter_get());
        }
    }

    batt_health_update(telem->charging, telem->batt_mv);
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
        push_serial_telemetry(&telem);
    }

    if (current_page == PAGE_STOPWATCH) stopwatch_update();
}

// -- Sub-loop: screen timeout only (deep sleep disabled) --
static void loop_power_management(void) {
    if (screen_is_on() && millis() - last_activity_time > DISPLAY_TIMEOUT_MS) {
        set_backlight(false);
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

// -- BOOT button handler (GPIO interrupt + debounce) --
static void loop_boot_button(void) {
    if (!boot_btn_pressed) return;
    boot_btn_pressed = false;

    unsigned long now = millis();
    if (now - boot_press_time < 200) return;
    boot_press_time = now;

    delay(10);
    bool long_press = false;
    while (digitalRead(BOOT_BTN) == LOW) {
        if (millis() - now > 1000) {
            long_press = true;
            break;
        }
        delay(10);
    }

    if (long_press) {
        // If recording, long press dismisses
        if (va_get_state() != VA_IDLE) {
            va_dismiss();
            LOG_INFO("BOOT long press: voice dismissed");
        } else {
            reset_activity_timer();
            current_face = watch_face_next(current_face);
            nvs_set_watch_face(current_face);
            if (current_face == FACE_ANALOG) {
                lv_scr_load(pages[PAGE_ANALOG]);
            } else if (current_face == FACE_SPORT) {
                lv_scr_load(sport_page);
            } else {
                lv_scr_load(pages[PAGE_DIGITAL]);
            }
            LOG_INFO("BOOT long press: face=%s", watch_face_name(current_face));
        }
    } else {
        reset_activity_timer();
        // If recording, short press stops recording
        if (va_get_state() == VA_RECORDING) {
            va_stop_recording();
            LOG_INFO("BOOT short press: stop recording");
        } else if (screen_is_on()) {
            set_backlight(false);
        } else {
            set_backlight(true);
        }
        LOG_INFO("BOOT short press: screen=%s", screen_is_on() ? "on" : "off");
    }
}

// -- PWR button handler (poll AXP2101 PWR key registers) --
static void loop_pwr_button(void) {
    static unsigned long last_pwr_check = 0;
    if (millis() - last_pwr_check < 100) return;
    last_pwr_check = millis();

    uint64_t irq = pmu.getIrqStatus();
    if (irq == 0) return;

    if (pmu.isPekeyLongPressIrq()) {
        pmu.clearIrqStatus();
        reset_activity_timer();
        if (screen_is_on()) {
            set_backlight(false);
        } else {
            set_backlight(true);
        }
        LOG_INFO("PWR long press: screen=%s", screen_is_on() ? "on" : "off");
    } else if (pmu.isPekeyShortPressIrq()) {
        pmu.clearIrqStatus();
        reset_activity_timer();
        if (quick_panel_is_visible()) {
            quick_panel_hide();
        } else {
            quick_panel_show();
        }
        LOG_INFO("PWR short press: quick panel");
    } else {
        pmu.clearIrqStatus();
    }
}

// ===========================================================
// Main loop
// ===========================================================
void loop() {
    lv_timer_handler();
    wifi_ntp_loop();
    loop_serial_commands();
    va_process();
    loop_communication();
    loop_telemetry();
    loop_power_management();
    loop_gesture();
    loop_boot_button();
    loop_pwr_button();
    delay(2);
}
