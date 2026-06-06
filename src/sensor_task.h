#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

// IMU data structure (mirrors SensorQMI8658 IMUdata)
typedef struct {
    float x, y, z;
} imu_data_t;

class SensorQMI8658;  // forward declaration

// IMU data (shared, protected by mutex)
extern imu_data_t acc;
extern imu_data_t gyr;

// Launch sensor reading task on Core 0
void sensor_task_start(SensorQMI8658 *imu_ptr);

// Lock/unlock IMU data mutex (for main loop reading acc/gyr)
void sensor_data_lock(void);
void sensor_data_unlock(void);

#endif // SENSOR_TASK_H
