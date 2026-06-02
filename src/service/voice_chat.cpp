// Voice chat service: WebSocket server + audio streaming
// Watch acts as WebSocket server (port 8765), phone connects as client

#include "voice_chat.h"
#include "audio.h"
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

static WebSocketsServer wsServer = WebSocketsServer(8765);
static volatile voice_state_t voice_state = VOICE_IDLE;
static uint8_t ws_client_id = 0;
static bool ws_client_connected = false;

static char response_text[512] = {0};
static char transcription[256] = {0};
static char api_key[128] = {0};

static TaskHandle_t send_task_handle = NULL;

// --- WebSocket event handler ---
static void ws_event_handler(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            IPAddress ip = wsServer.remoteIP(num);
            USBSerial.printf("WS: client %u connected from %s\n", num, ip.toString().c_str());
            ws_client_id = num;
            ws_client_connected = true;
            break;
        }
        case WStype_TEXT: {
            DynamicJsonDocument doc(1024);
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) { USBSerial.printf("WS: JSON parse error: %s\n", err.c_str()); return; }

            const char *msg_type = doc["type"];
            if (!msg_type) return;

            if (strcmp(msg_type, "response") == 0) {
                const char *text = doc["text"] | "";
                const char *trans = doc["transcription"] | "";
                strncpy(response_text, text, sizeof(response_text) - 1);
                strncpy(transcription, trans, sizeof(transcription) - 1);
                voice_state = VOICE_RESPONSE;
                USBSerial.printf("WS: response: %s\n", response_text);
            } else if (strcmp(msg_type, "tts_audio") == 0) {
                // Optional: receive base64-encoded TTS PCM
                voice_state = VOICE_PLAYING_TTS;
            } else if (strcmp(msg_type, "error") == 0) {
                const char *msg = doc["message"] | "Unknown error";
                strncpy(response_text, msg, sizeof(response_text) - 1);
                voice_state = VOICE_IDLE;
                USBSerial.printf("WS: error: %s\n", response_text);
            } else if (strcmp(msg_type, "info") == 0) {
                USBSerial.printf("WS: info: %s\n", doc["message"] | "");
            }
            break;
        }
        case WStype_BIN:
            // Binary TTS audio from phone (future use)
            break;
        case WStype_DISCONNECTED:
            USBSerial.printf("WS: client %u disconnected\n", num);
            ws_client_connected = false;
            break;
        default:
            break;
    }
}

// --- Audio streaming task ---
// Runs while recording: reads chunks from I2S RX, sends via WebSocket
static void voice_send_task(void *param) {
    int16_t chunk[512];  // 1024 bytes = 512 samples = 32ms at 16kHz

    // Send start signal
    DynamicJsonDocument start_doc(128);
    start_doc["type"] = "start";
    start_doc["sample_rate"] = VOICE_SAMPLE_RATE;
    start_doc["bits"] = VOICE_BITS_PER_SAMPLE;
    start_doc["channels"] = 1;
    char start_buf[128];
    serializeJson(start_doc, start_buf, sizeof(start_buf));
    wsServer.sendTXT(ws_client_id, start_buf);

    voice_state = VOICE_RECORDING;
    USBSerial.println("Streaming audio...");

    while (voice_state == VOICE_RECORDING) {
        int samples = audio_read_chunk(chunk, 512);
        if (samples > 0 && ws_client_connected) {
            // Frame: [type=0x01][len_lo][len_hi][PCM data...]
            uint16_t byte_len = samples * sizeof(int16_t);
            uint8_t *frame = (uint8_t *)malloc(3 + byte_len);
            if (frame) {
                frame[0] = 0x01;  // audio chunk marker
                frame[1] = byte_len & 0xFF;
                frame[2] = (byte_len >> 8) & 0xFF;
                memcpy(frame + 3, chunk, byte_len);
                wsServer.sendBIN(ws_client_id, frame, 3 + byte_len);
                free(frame);
            }
        }
    }

    // Send end signal
    if (ws_client_connected) {
        wsServer.sendTXT(ws_client_id, "{\"type\":\"end\"}");
    }

    voice_state = VOICE_WAITING;
    USBSerial.println("Waiting for response...");
    send_task_handle = NULL;
    vTaskDelete(NULL);
}

// --- Public API ---

void voice_chat_init(void) {
    wsServer.begin();
    wsServer.onEvent(ws_event_handler);
    USBSerial.printf("Voice chat: WebSocket server on port 8765\n");
    USBSerial.printf("Voice chat: connect from phone to ws://%s:8765\n",
                     WiFi.localIP().toString().c_str());
}

void voice_chat_start_recording(void) {
    if (voice_state == VOICE_RECORDING) return;
    if (!ws_client_connected) {
        USBSerial.println("Voice: no WS client connected");
        return;
    }
    memset(response_text, 0, sizeof(response_text));
    memset(transcription, 0, sizeof(transcription));
    audio_start_recording();
    xTaskCreatePinnedToCore(voice_send_task, "voice_send", 4096, NULL, 3, &send_task_handle, 0);
}

void voice_chat_stop_recording(void) {
    if (voice_state != VOICE_RECORDING) return;
    audio_stop_recording();
    // The send task will detect voice_state change and send "end"
}

voice_state_t voice_chat_get_state(void) { return voice_state; }
const char* voice_chat_get_response(void) { return response_text; }
const char* voice_chat_get_transcription(void) { return transcription; }
void voice_chat_set_api_key(const char *key) { strncpy(api_key, key, sizeof(api_key) - 1); }

// Call this periodically to let WebSocket process events
void voice_chat_loop(void) {
    wsServer.loop();
}
