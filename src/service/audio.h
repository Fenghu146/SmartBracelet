#pragma once
#include <stdint.h>

// --- Speaker (I2S_NUM_0, ES8311) ---
bool audio_init(void);
bool audio_play_sine(int freq_hz, int duration_ms);
bool audio_play_wav(const char *path);
void audio_stop(void);
void audio_set_volume(uint8_t vol);  // 0-100
bool audio_is_playing(void);
void audio_play_response(const int16_t *data, int samples, int sample_rate);

// --- Microphone (I2S_NUM_1, INMP441) ---
#define VOICE_SAMPLE_RATE    16000
#define VOICE_BITS_PER_SAMPLE 16
#define VOICE_MAX_RECORD_SEC 8

bool audio_init_rx(void);
bool audio_start_recording(void);
int  audio_read_chunk(int16_t *buf, int max_samples);  // returns sample count (0=timeout)
void audio_stop_recording(void);
bool audio_is_recording(void);
