#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Voice Assistant — local recording via INMP441 mic
// ============================================================

// State machine
typedef enum {
    VA_IDLE,          // Waiting for trigger
    VA_RECORDING,     // Mic active, compressing audio to buffer
    VA_SENDING,       // Recording done, transmitting ADPCM over serial
    VA_WAITING,       // Audio sent, waiting for PC transcription+response
    VA_RESPONSE,      // Results displayed, waiting for user to dismiss
    VA_ERROR,         // Error state (auto-returns to IDLE after delay)
} va_state_t;

// Progress callback type for UI
typedef void (*va_progress_cb_t)(float progress);

// Initialize voice assistant
void va_init(void);

// Start recording (triggered by UI button or BOOT key)
// Returns false if audio_init_rx() failed or resources exhausted
bool va_start_recording(void);

// Stop recording (triggered by UI button, BOOT key, or auto max duration)
// Returns immediately; actual stop is async
bool va_stop_recording(void);

// Process – call from main loop every iteration (~2ms)
// Handles: pulling PCM from ring buffer, ADPCM compression, serial TX, state transitions
void va_process(void);

// Called from serial_protocol when a voice result arrives from PC
// Format: {"c":"voice","vc":"va_result","trans":"...","resp":"..."}
void va_on_result(const char *transcription, const char *response);

// Called from serial_protocol when an error arrives from PC
void va_on_error(const char *msg);

// --- Accessors for UI ---
va_state_t    va_get_state(void);
const char*   va_get_transcription(void);
const char*   va_get_response(void);
const char*   va_get_error_msg(void);
float         va_get_progress(void);   // 0.0 – 1.0 during RECORDING or SENDING

// Register progress callback (for UI animation)
void va_set_progress_callback(va_progress_cb_t cb);

// Reset state back to IDLE (after user dismisses)
void va_dismiss(void);

// Limits
#define VA_MAX_RECORD_MS     8000   // max recording duration
#define VA_SAMPLE_RATE       16000

#ifdef __cplusplus
}
#endif
