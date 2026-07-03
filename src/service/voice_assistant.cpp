// Voice assistant — local recording via INMP441 → ADPCM compress → serial → desktop app → cloud AI → display
#include "voice_assistant.h"
#include "adpcm.h"
#include "audio.h"
#include "../serial_protocol.h"
#include "../debug_log.h"
#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================
// Configuration
// ============================================================

// ADPCM buffer: 8 seconds × 16kHz × 16bit / 2 (4:1 compression) + margin
#define VA_ADPCM_BUF_SIZE       ((VA_MAX_RECORD_MS * VA_SAMPLE_RATE * 2) / (2 * 1000) + 512)
// Bytes of ADPCM data per serial chunk
#define VA_SERIAL_CHUNK_BYTES   1024
// Minimum interval between serial chunks (ms)
#define VA_SEND_INTERVAL_MS     100
// Auto-return to IDLE from ERROR (ms)
#define VA_ERROR_TIMEOUT_MS     4000

// ============================================================
// Internal state
// ============================================================

static va_state_t       g_state = VA_IDLE;
static adpcm_state_t    g_adpcm_state;
static uint8_t         *g_adpcm_buf = NULL;
static int              g_adpcm_pos = 0;        // bytes written so far
static int              g_adpcm_total = 0;      // total bytes when recording stopped
static int              g_send_seq = 0;         // current chunk sequence
static int              g_send_total = 0;       // total chunks to send
static unsigned long    g_rec_start_ms = 0;
static unsigned long    g_error_start_ms = 0;
static char             g_transcription[512] = "";
static char             g_response[1024] = "";
static char             g_error_msg[128] = "";
static va_progress_cb_t g_progress_cb = NULL;

// ============================================================
// Internal helpers
// ============================================================

// Simple Base64 encoder (~15 lines, no mbedtls dependency)
static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max) {
    size_t i = 0, o = 0;
    while (i < in_len && o + 4 <= out_max) {
        uint32_t b = (i < in_len ? in[i++] : 0) << 16;
        b |= (i < in_len ? in[i++] : 0) << 8;
        b |= (i < in_len ? in[i++] : 0);
        out[o++] = b64_alphabet[(b >> 18) & 0x3F];
        out[o++] = b64_alphabet[(b >> 12) & 0x3F];
        out[o++] = (i - 1 < in_len) ? b64_alphabet[(b >> 6) & 0x3F] : '=';
        out[o++] = (i - 2 < in_len) ? b64_alphabet[b & 0x3F] : '=';
    }
    out[o] = '\0';
    return o;
}

static void free_adpcm_buffer(void) {
    if (g_adpcm_buf) {
        free(g_adpcm_buf);
        g_adpcm_buf = NULL;
    }
}

// ============================================================
// Public API
// ============================================================

void va_init(void) {
    g_state = VA_IDLE;
    g_adpcm_buf = NULL;
    LOG_INFO("VA: ready (mic-assisted)");
}

bool va_start_recording(void) {
    if (g_state != VA_IDLE) return false;

    // Allocate ADPCM buffer: prefer PSRAM, fall back to SRAM
    if (psramFound()) {
        g_adpcm_buf = (uint8_t*)heap_caps_malloc(VA_ADPCM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (!g_adpcm_buf) {
        g_adpcm_buf = (uint8_t*)malloc(VA_ADPCM_BUF_SIZE);
    }
    if (!g_adpcm_buf) {
        LOG_ERR("VA: malloc(%d) failed", VA_ADPCM_BUF_SIZE);
        return false;
    }

    // Initialize microphone I2S RX (idempotent — safe to call multiple times)
    if (!audio_init_rx()) {
        LOG_ERR("VA: audio_init_rx failed");
        free_adpcm_buffer();
        return false;
    }

    adpcm_reset(&g_adpcm_state);
    g_adpcm_pos = 0;
    g_rec_start_ms = millis();
    audio_start_recording();
    g_state = VA_RECORDING;
    LOG_INFO("VA: recording started");
    return true;
}

bool va_stop_recording(void) {
    if (g_state != VA_RECORDING) return false;

    audio_stop_recording();

    g_adpcm_total = g_adpcm_pos;
    g_send_seq = 0;
    g_send_total = (g_adpcm_total + VA_SERIAL_CHUNK_BYTES - 1) / VA_SERIAL_CHUNK_BYTES;

    // Reset transcription/response for new session
    g_transcription[0] = '\0';
    g_response[0] = '\0';

    // Notify desktop app that audio transmission is starting
    serial_push_audio_start(g_adpcm_total);

    g_state = VA_SENDING;
    LOG_INFO("VA: stopped, %d bytes ADPCM (%d chunks)", g_adpcm_total, g_send_total);
    return true;
}

void va_process(void) {
    switch (g_state) {

    case VA_IDLE:
        // Nothing to do
        break;

    case VA_RECORDING: {
        // Check max duration → auto-stop
        unsigned long elapsed = millis() - g_rec_start_ms;
        if (elapsed >= VA_MAX_RECORD_MS) {
            LOG_INFO("VA: max duration reached");
            va_stop_recording();
            break;
        }

        // Read PCM chunk from ring buffer
        int16_t pcm_chunk[512];
        int samples = audio_read_chunk(pcm_chunk, 512);
        if (samples > 0) {
            // Encode to ADPCM
            int remaining = VA_ADPCM_BUF_SIZE - g_adpcm_pos;
            if (remaining >= samples / 2) {
                int encoded = adpcm_encode_buf(pcm_chunk, samples,
                                                g_adpcm_buf + g_adpcm_pos,
                                                &g_adpcm_state);
                g_adpcm_pos += encoded;
            } else {
                LOG_WARN("VA: ADPCM buffer full (%d/%d)", g_adpcm_pos, VA_ADPCM_BUF_SIZE);
                va_stop_recording();
                break;
            }
        }

        // Update progress for idle UI
        float pct = (float)elapsed / VA_MAX_RECORD_MS;
        if (pct > 1.0f) pct = 1.0f;
        if (g_progress_cb) g_progress_cb(pct);
        break;
    }

    case VA_SENDING: {
        if (g_send_seq >= g_send_total) {
            // All chunks sent
            serial_push_audio_end(g_send_seq - 1);
            g_state = VA_WAITING;
            LOG_INFO("VA: all %d chunks sent, waiting for result", g_send_total);
            break;
        }

        // Rate-limit: one chunk per VA_SEND_INTERVAL_MS
        static unsigned long last_send = 0;
        unsigned long now = millis();
        if (now - last_send < VA_SEND_INTERVAL_MS) break;
        last_send = now;

        int offset = g_send_seq * VA_SERIAL_CHUNK_BYTES;
        int chunk_size = VA_SERIAL_CHUNK_BYTES;
        if (offset + chunk_size > g_adpcm_total) {
            chunk_size = g_adpcm_total - offset;
        }

        // Base64 encode
        char b64_buf[2048];
        int b64_len = base64_encode(g_adpcm_buf + offset, chunk_size,
                                     b64_buf, sizeof(b64_buf));
        if (b64_len > 0) {
            b64_buf[b64_len] = '\0';
            serial_push_audio_chunk(g_send_seq, b64_buf);
        } else {
            serial_push_audio_chunk(g_send_seq, "");
        }
        g_send_seq++;

        // Update progress
        if (g_progress_cb) {
            g_progress_cb((float)g_send_seq / g_send_total);
        }
        break;
    }

    case VA_WAITING:
        // Nothing to do; va_on_result/va_on_error will transition state
        break;

    case VA_RESPONSE:
        // Nothing to do; user taps to dismiss via va_dismiss()
        break;

    case VA_ERROR:
        // Auto-return to IDLE after timeout
        if (millis() - g_error_start_ms >= VA_ERROR_TIMEOUT_MS) {
            g_state = VA_IDLE;
            free_adpcm_buffer();
            LOG_INFO("VA: auto-dismissed after error");
        }
        break;
    }
}

void va_on_result(const char *transcription, const char *response) {
    if (transcription) {
        strncpy(g_transcription, transcription, sizeof(g_transcription) - 1);
        g_transcription[sizeof(g_transcription) - 1] = '\0';
    }
    if (response) {
        strncpy(g_response, response, sizeof(g_response) - 1);
        g_response[sizeof(g_response) - 1] = '\0';
    }
    g_state = VA_RESPONSE;
    free_adpcm_buffer();
    LOG_INFO("VA: result received");
}

void va_on_error(const char *msg) {
    strncpy(g_error_msg, msg ? msg : "Unknown error", sizeof(g_error_msg) - 1);
    g_error_msg[sizeof(g_error_msg) - 1] = '\0';
    g_state = VA_ERROR;
    g_error_start_ms = millis();
    free_adpcm_buffer();
    LOG_INFO("VA: error: %s", g_error_msg);
}

va_state_t va_get_state(void) { return g_state; }
const char* va_get_transcription(void) { return g_transcription; }
const char* va_get_response(void) { return g_response; }
const char* va_get_error_msg(void) { return g_error_msg; }

float va_get_progress(void) {
    if (g_state == VA_RECORDING) {
        unsigned long elapsed = millis() - g_rec_start_ms;
        float pct = (float)elapsed / VA_MAX_RECORD_MS;
        return (pct > 1.0f) ? 1.0f : pct;
    }
    if (g_state == VA_SENDING && g_send_total > 0) {
        return (float)g_send_seq / g_send_total;
    }
    if (g_state == VA_RESPONSE || g_state == VA_WAITING) return 1.0f;
    return 0.0f;
}

void va_set_progress_callback(va_progress_cb_t cb) { g_progress_cb = cb; }

void va_dismiss(void) {
    g_state = VA_IDLE;
    g_transcription[0] = '\0';
    g_response[0] = '\0';
}
