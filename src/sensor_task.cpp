// Sensor task: reads IMU at ~125Hz on Core 0, processes data
// Main loop on Core 1 reads results via mutex-protected shared data
#include "sensor_task.h"
#include "SensorQMI8658.hpp"  // Full IMU driver header
#include "step_counter.h"
#include "wrist_detect.h"
#include "fall_detect.h"
#include "motion_intensity.h"
#include "activity.h"
#include "debug_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static SensorQMI8658 *sensor_imu = nullptr;
static SemaphoreHandle_t data_mutex = nullptr;
static TaskHandle_t sensor_task_handle = nullptr;

imu_data_t acc, gyr;

void sensor_data_lock(void) {
    if (data_mutex) xSemaphoreTake(data_mutex, portMAX_DELAY);
}

void sensor_data_unlock(void) {
    if (data_mutex) xSemaphoreGive(data_mutex);
}

static void sensor_reading_task(void *param) {
    LOG_INFO("Sensor task: started on core %d", xPortGetCoreID());

    const TickType_t period = pdMS_TO_TICKS(8);  // ~125Hz
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        if (sensor_imu && sensor_imu->getDataReady()) {
            float ax, ay, az, gx, gy, gz;
            sensor_imu->getAccelerometer(ax, ay, az);
            sensor_imu->getGyroscope(gx, gy, gz);

            // Write to shared data under mutex
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(2))) {
                acc.x = ax; acc.y = ay; acc.z = az;
                gyr.x = gx; gyr.y = gy; gyr.z = gz;
                xSemaphoreGive(data_mutex);
            }

            // Process data (these are quick math, no I/O)
            activity_push_data(ax, ay, az, gx, gy, gz);
            step_counter_update(ax, ay, az);
            wrist_detect_update(ax, ay, az);
            fall_detect_update(ax, ay, az);
            motion_intensity_update(ax, ay, az);
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

void sensor_task_start(SensorQMI8658 *imu_ptr) {
    sensor_imu = imu_ptr;

    // Create mutex for IMU data protection
    if (!data_mutex) {
        data_mutex = xSemaphoreCreateMutex();
    }

    // Create sensor task on Core 0, high priority
    xTaskCreatePinnedToCore(
        sensor_reading_task,
        "sensor",
        4096,           // stack size
        nullptr,        // param
        configMAX_PRIORITIES - 1,  // highest priority
        &sensor_task_handle,
        0               // pin to Core 0
    );

    LOG_INFO("Sensor task: created on Core 0 at 125Hz");
}
