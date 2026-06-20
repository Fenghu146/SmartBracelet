// Voice chat service �?receives commands from phone via BLE
// Phone records audio, calls cloud APIs, sends results back

#include "voice_chat.h"
#include "ble_srv.h"

static volatile voice_state_t voice_state = VOICE_IDLE;
static char response_text[512] = {0};
static char transcription[256] = {0};

// BLE voice command handler
static void on_voice_cmd(const char *cmd, const char *arg) {
    if (strcmp(cmd, "start") == 0) {
        voice_state = VOICE_RECORDING;
        USBSerial.println("Voice: phone started recording");
    } else if (strcmp(cmd, "stop") == 0) {
        voice_state = VOICE_WAITING;
        USBSerial.println("Voice: phone stopped recording, processing...");
    } else if (strcmp(cmd, "result") == 0) {
        // arg contains "transcription|response"
        const char *sep = strchr(arg, '|');
        if (sep) {
            int trans_len = sep - arg;
            if (trans_len >= (int)sizeof(transcription)) trans_len = sizeof(transcription) - 1;
            strncpy(transcription, arg, trans_len);
            transcription[trans_len] = '\0';
            strncpy(response_text, sep + 1, sizeof(response_text) - 1);
        } else {
            strncpy(transcription, arg, sizeof(transcription) - 1);
            response_text[0] = '\0';
        }
        voice_state = VOICE_RESPONSE;
        USBSerial.printf("Voice result: %s -> %s\n", transcription, response_text);
    } else if (strcmp(cmd, "error") == 0) {
        strncpy(response_text, arg, sizeof(response_text) - 1);
        voice_state = VOICE_IDLE;
        USBSerial.printf("Voice error: %s\n", response_text);
    }
}

void voice_chat_init(void) {
    ble_srv_set_voice_cmd_callback(on_voice_cmd);
    USBSerial.println("Voice chat: ready (phone-initiated)");
}

voice_state_t voice_chat_get_state(void) { return voice_state; }
const char* voice_chat_get_response(void) { return response_text; }
const char* voice_chat_get_transcription(void) { return transcription; }
