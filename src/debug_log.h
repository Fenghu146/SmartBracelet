#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>

// Log levels: 0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
// Set via build flag -DLOG_LEVEL=4 or change default here
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#if LOG_LEVEL >= 1
#define LOG_ERR(fmt, ...)  USBSerial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_ERR(fmt, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_WARN(fmt, ...) USBSerial.printf("[W] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= 3
#define LOG_INFO(fmt, ...) USBSerial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= 4
#define LOG_DEBUG(fmt, ...) USBSerial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#if LOG_LEVEL >= 5
#define LOG_VERBOSE(fmt, ...) USBSerial.printf("[V] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_VERBOSE(fmt, ...)
#endif

#endif // DEBUG_LOG_H
