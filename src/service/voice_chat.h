#pragma once
#include <Arduino.h>

typedef enum {
    VOICE_IDLE,
    VOICE_RECORDING,    // phone is recording
    VOICE_WAITING,      // waiting for cloud result
    VOICE_RESPONSE,     // showing result
} voice_state_t;

void voice_chat_init(void);
void voice_chat_on_command(const char *cmd, const char *arg);
voice_state_t voice_chat_get_state(void);
const char* voice_chat_get_response(void);
const char* voice_chat_get_transcription(void);
