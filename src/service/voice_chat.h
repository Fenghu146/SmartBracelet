#pragma once
#include <Arduino.h>

typedef enum {
    VOICE_IDLE,
    VOICE_RECORDING,
    VOICE_SENDING,
    VOICE_WAITING,
    VOICE_RESPONSE,
    VOICE_PLAYING_TTS,
} voice_state_t;

void voice_chat_init(void);
void voice_chat_start_recording(void);
void voice_chat_stop_recording(void);
voice_state_t voice_chat_get_state(void);
const char* voice_chat_get_response(void);
const char* voice_chat_get_transcription(void);
void voice_chat_set_api_key(const char *key);
void voice_chat_loop(void);
