#include "ota_update.h"
#include "wifi_ntp.h"
#include "../debug_log.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static volatile ota_state_t ota_state = OTA_IDLE;
static volatile int ota_progress = 0;
static char ota_error[128] = {0};
static char ota_url[256] = {0};
static volatile bool ota_restart_pending = false;
static unsigned long ota_restart_time = 0;
static SemaphoreHandle_t ota_mutex = nullptr;
static TaskHandle_t ota_task_handle = nullptr;

// Total firmware size from Content-Length header
static int ota_total_size = 0;
static int ota_written = 0;

// ── OTA download task (runs on Core 0) ──
static void ota_download_task(void *param) {
    LOG_INFO("OTA task: started on core %d", xPortGetCoreID());

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    http.setUserAgent("SmartBracelet-OTA/1.0");

    LOG_INFO("OTA task: connecting to %s", ota_url);
    if (!http.begin(ota_url)) {
        strncpy(ota_error, "Failed to connect to URL", sizeof(ota_error) - 1);
        ota_state = OTA_ERROR;
        LOG_ERR("OTA task: %s", ota_error);
        ota_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        snprintf(ota_error, sizeof(ota_error), "HTTP error: %d", httpCode);
        ota_state = OTA_ERROR;
        LOG_ERR("OTA task: %s", ota_error);
        http.end();
        ota_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    ota_total_size = http.getSize();
    if (ota_total_size <= 0) {
        strncpy(ota_error, "Invalid firmware size", sizeof(ota_error) - 1);
        ota_state = OTA_ERROR;
        LOG_ERR("OTA task: %s", ota_error);
        http.end();
        ota_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    LOG_INFO("OTA task: firmware size=%d bytes", ota_total_size);

    if (!Update.begin(ota_total_size)) {
        snprintf(ota_error, sizeof(ota_error), "Not enough space: %d bytes", ota_total_size);
        ota_state = OTA_ERROR;
        LOG_ERR("OTA task: %s", ota_error);
        http.end();
        ota_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    ota_state = OTA_WRITING;
    WiFiClient *stream = http.getStreamPtr();

    uint8_t buf[4096];
    ota_written = 0;

    while (ota_written < ota_total_size) {
        size_t avail = stream->available();
        if (avail == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t toRead = min((size_t)sizeof(buf), avail);
        size_t bytesRead = stream->readBytes(buf, toRead);

        if (bytesRead == 0) {
            if (!http.connected() && ota_written < ota_total_size) {
                snprintf(ota_error, sizeof(ota_error),
                    "Connection lost at %d/%d bytes", ota_written, ota_total_size);
                ota_state = OTA_ERROR;
                Update.abort();
                http.end();
                LOG_ERR("OTA task: %s", ota_error);
                ota_task_handle = nullptr;
                vTaskDelete(NULL);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t written = Update.write(buf, bytesRead);
        if (written != bytesRead) {
            snprintf(ota_error, sizeof(ota_error),
                "Write error: wrote %d of %d", written, bytesRead);
            ota_state = OTA_ERROR;
            Update.abort();
            http.end();
            LOG_ERR("OTA task: %s", ota_error);
            ota_task_handle = nullptr;
            vTaskDelete(NULL);
            return;
        }

        ota_written += written;
        ota_progress = (ota_written * 100) / ota_total_size;

        // Yield periodically to avoid WDT
        if (ota_written % (64 * 1024) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    http.end();

    // Verify and apply
    ota_state = OTA_VERIFYING;
    if (Update.end(true)) {
        ota_progress = 100;
        ota_state = OTA_SUCCESS;
        LOG_INFO("OTA task: success! %d bytes written. Reboot in 3s...", ota_written);
        ota_restart_pending = true;
        ota_restart_time = millis() + 3000;
    } else {
        snprintf(ota_error, sizeof(ota_error),
            "Verify failed: error %d", Update.getError());
        ota_state = OTA_ERROR;
        LOG_ERR("OTA task: %s", ota_error);
    }

    ota_task_handle = nullptr;
    vTaskDelete(NULL);
}

// ── Public API ──

bool ota_start(const char *url) {
    if (ota_state == OTA_DOWNLOADING || ota_state == OTA_WRITING) {
        strncpy(ota_error, "OTA already in progress", sizeof(ota_error) - 1);
        return false;
    }

    if (!wifi_is_connected()) {
        strncpy(ota_error, "WiFi not connected", sizeof(ota_error) - 1);
        ota_state = OTA_ERROR;
        return false;
    }

    strncpy(ota_url, url, sizeof(ota_url) - 1);
    ota_url[sizeof(ota_url) - 1] = '\0';
    ota_progress = 0;
    ota_error[0] = '\0';
    ota_total_size = 0;
    ota_written = 0;
    ota_state = OTA_DOWNLOADING;

    LOG_INFO("OTA: launching download task on Core 0");

    // Create task on Core 0 with generous stack for HTTP+Update
    xTaskCreatePinnedToCore(
        ota_download_task,
        "ota_task",
        8192,          // stack size
        nullptr,       // param
        3,             // priority (medium)
        &ota_task_handle,
        0              // pin to Core 0 (leave Core 1 for UI)
    );

    return true;
}

ota_state_t ota_get_state(void) {
    return (ota_state_t)ota_state;
}

int ota_get_progress(void) {
    return ota_progress;
}

const char* ota_get_error(void) {
    return ota_error;
}

bool ota_check_restart(void) {
    if (ota_restart_pending && millis() > ota_restart_time) {
        LOG_INFO("OTA: restarting...");
        return true;
    }
    return false;
}

// ── BLE OTA ──

static uint32_t ble_ota_total = 0;
static uint32_t ble_ota_written = 0;

bool ota_start_ble(uint32_t total_size) {
    if (ota_state == OTA_DOWNLOADING || ota_state == OTA_WRITING) {
        strncpy(ota_error, "OTA already in progress", sizeof(ota_error) - 1);
        return false;
    }

    ble_ota_total = total_size;
    ble_ota_written = 0;
    ota_progress = 0;
    ota_error[0] = '\0';

    LOG_INFO("BLE OTA: starting, size=%u bytes", total_size);

    if (!Update.begin(total_size)) {
        snprintf(ota_error, sizeof(ota_error), "Not enough space: %u", total_size);
        ota_state = OTA_ERROR;
        LOG_ERR("BLE OTA: %s", ota_error);
        return false;
    }

    ota_state = OTA_WRITING;
    return true;
}

bool ota_write_chunk_ble(const uint8_t *data, size_t len) {
    if (ota_state != OTA_WRITING) return false;

    size_t written = Update.write((uint8_t *)data, len);
    if (written != len) {
        snprintf(ota_error, sizeof(ota_error), "Write error: %d of %d", written, len);
        ota_state = OTA_ERROR;
        Update.abort();
        LOG_ERR("BLE OTA: %s", ota_error);
        return false;
    }

    ble_ota_written += written;
    if (ble_ota_total > 0) {
        ota_progress = (ble_ota_written * 100) / ble_ota_total;
    }
    return true;
}

bool ota_end_ble(void) {
    if (ota_state != OTA_WRITING) return false;

    ota_state = OTA_VERIFYING;
    if (Update.end(true)) {
        ota_progress = 100;
        ota_state = OTA_SUCCESS;
        LOG_INFO("BLE OTA: success! %u bytes. Reboot in 3s...", ble_ota_written);
        ota_restart_pending = true;
        ota_restart_time = millis() + 3000;
        return true;
    } else {
        snprintf(ota_error, sizeof(ota_error), "Verify failed: error %d", Update.getError());
        ota_state = OTA_ERROR;
        LOG_ERR("BLE OTA: %s", ota_error);
        return false;
    }
}
