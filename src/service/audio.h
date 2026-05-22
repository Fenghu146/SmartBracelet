#pragma once
#include <stdint.h>

bool audio_init(void);
bool audio_play_sine(int freq_hz, int duration_ms);
bool audio_play_wav(const char *path);
void audio_stop(void);
void audio_set_volume(uint8_t vol);  // 0-100
bool audio_is_playing(void);
