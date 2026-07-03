#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <Arduino.h>
#include "ui_pages.h"

// Push telemetry JSON line to USB serial (watch → PC)
void serial_push_telemetry(const ui_telemetry_t *t);

// Push event/alert JSON line to USB serial (watch → PC)
void serial_push_event(const char *type, const char *msg);

// Push audio transfer events (watch → PC, voice assistant)
void serial_push_audio_start(int total_adpcm_bytes);
void serial_push_audio_chunk(int seq, const char *base64_data);
void serial_push_audio_end(int last_seq);

// Parse and dispatch incoming JSON commands from USB serial (PC → watch)
// Call this in the main loop
void serial_protocol_process(void);

// Check if a new notification arrived via serial
bool serial_notification_has_new(void);

// Get the latest notification data
const char* serial_notification_app(void);
const char* serial_notification_title(void);
const char* serial_notification_body(void);
void serial_notification_consume(void);

#endif
