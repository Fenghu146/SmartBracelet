#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>

// Use USBSerial for native USB port output
#define LOG_SERIAL USBSerial

// Log levels: 0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
// Set via build flag -DLOG_LEVEL=4 or change default here
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#if LOG_LEVEL >= 1
#define LOG_ERR(fmt, ...)  LOG_SERIAL.printf("[E] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_ERR(fmt, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_WARN(fmt, ...) LOG_SERIAL.printf("[W] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= 3
#define LOG_INFO(fmt, ...) LOG_SERIAL.printf("[I] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= 4
#define LOG_DEBUG(fmt, ...) LOG_SERIAL.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#if LOG_LEVEL >= 5
#define LOG_VERBOSE(fmt, ...) LOG_SERIAL.printf("[V] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_VERBOSE(fmt, ...)
#endif

#endif // DEBUG_LOG_H
