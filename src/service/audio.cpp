// ES8311 audio codec + PCA9557 I2C I/O expander (PA_EN control)
// Pin assignments verified against Waveshare ESP32-S3-Touch-LCD-1.83 schematic

#include "audio.h"
#include "pin_config.h"
#include "../debug_log.h"
#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <freertos/ringbuf.h>
#include <FS.h>
#include <SD_MMC.h>

// --- PCA9557 I2C I/O expander (address 0x19) ---
// IO0=LCD_CS, IO1=PA_EN, IO2=DVP_PWDN
#define PCA9557_INPUT     0x00
#define PCA9557_OUTPUT    0x01
#define PCA9557_INVERT    0x02
#define PCA9557_CONFIG    0x03
#define PA_EN_BIT         0x02  // BIT(1)

static bool pca9557_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool pca9557_init(void) {
  // Set IO0/IO1/IO2 as outputs (0=output), others as inputs (1=input)
  if (!pca9557_write(PCA9557_CONFIG, 0xF8)) {
    LOG_ERR("PCA9557: no response");
    return false;
  }
  // Set initial output: LCD_CS=1(high), PA_EN=1(high), DVP_PWDN=1(high)
  pca9557_write(PCA9557_OUTPUT, 0x07);
  LOG_INFO("PCA9557: PA_EN enabled");
  return true;
}

// --- ES8311 I2S audio codec (address 0x18) ---
// Register map from Everest ES8311 datasheet
#define ES8311_RESET    0x00  // reset + CSM_ON
#define ES8311_CLK1     0x01  // MCLK_SEL, MCLK_ON, BCLK_ON
#define ES8311_CLK2     0x02  // pre-divider, multiplier
#define ES8311_CLK3     0x03  // ADC OSR
#define ES8311_CLK4     0x04  // DAC OSR
#define ES8311_CLK5     0x05  // ADC/DAC clock divider
#define ES8311_CLK6     0x06  // BCLK config
#define ES8311_CLK7     0x07  // LRCK divider HI + tri-state
#define ES8311_CLK8     0x08  // LRCK divider LO
#define ES8311_SDP_IN   0x09  // Serial data port input (I2S format)
#define ES8311_SDP_OUT  0x0A  // Serial data port output
#define ES8311_PWR_A    0x0B  // power up sequence A
#define ES8311_PWR_B    0x0C  // power up sequence B
#define ES8311_PWR_C    0x0D  // power up sequence C
#define ES8311_PWR_D    0x0E  // power up sequence D
#define ES8311_PWR_E    0x0F  // power up sequence E
#define ES8311_VMID     0x10  // VMID, bias, ref
#define ES8311_VOLT     0x11  // internal voltage select
#define ES8311_ANA      0x12  // analog mux
#define ES8311_OUT      0x13  // output routing: BIT4=HPSW (0=line,1=headphone)
#define ES8311_ADC_CTL  0x16  // ADC control + gain
#define ES8311_ADC_HPF  0x1B  // ADC auto-mute, HPF
#define ES8311_ADC_FLT  0x1C  // ADC filter
#define ES8311_ADC_CTL2 0x1D  // ADC control 2
#define ES8311_DAC_CTL1 0x31  // DAC control: mute
#define ES8311_DAC_VOL  0x32  // DAC volume: 0x00=-95dB ... 0xBF=0dB ... 0xFF=+32dB
#define ES8311_DAC_MISC 0x37  // DAC misc (default 0x08)
#define ES8311_GPIO     0x44  // GPIO function, ADCDAT_SEL

static bool es8311_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool es8311_init(void) {
  // 1. Reset
  if (!es8311_write(ES8311_RESET, 0x1F)) {
    LOG_ERR("ES8311: I2C no response");
    return false;
  }
  delay(10);
  es8311_write(ES8311_RESET, 0x02);  // clear reset, keep SEQ_DIS
  delay(50);

  // 2. Clock config (MCLK from I2S, 44.1kHz, MCLK=256FS)
  es8311_write(ES8311_CLK1, 0x30);  // MCLK_ON + BCLK_ON
  es8311_write(ES8311_CLK2, 0x00);  // pre=1, mult=1
  es8311_write(ES8311_CLK3, 0x10);  // ADC OSR=64
  es8311_write(ES8311_CLK4, 0x10);  // DAC OSR=64
  es8311_write(ES8311_CLK5, 0x00);  // div_clk_adc=0, div_clk_dac=0
  es8311_write(ES8311_CLK6, 0x03);  // BCLK continuous, div=3

  // 3. Digital audio interface: I2S, 24-bit
  es8311_write(ES8311_SDP_IN,  0x08);  // I2S, 16-bit
  es8311_write(ES8311_SDP_OUT, 0x00);

  // 4. Power up
  es8311_write(ES8311_PWR_A, 0x00);
  es8311_write(ES8311_PWR_B, 0x00);
  es8311_write(ES8311_PWR_C, 0x1F);
  es8311_write(ES8311_PWR_D, 0x1F);
  es8311_write(ES8311_PWR_E, 0x1F);

  // 5. Analog: VMID, bias, reference
  es8311_write(ES8311_VMID, 0x1F);  // VMID=high, bias=normal, ref=enabled
  es8311_write(ES8311_VOLT, 0x7F);  // internal voltage

  // 6. Output routing: line out (not headphone amp)
  es8311_write(ES8311_OUT, 0x00);   // HPSW=0 鈫?line output mode

  // 7. Start state machine
  es8311_write(ES8311_RESET, 0x80); // CSM_ON=1 (slave mode)

  // 8. Unmute + set volume
  es8311_write(ES8311_DAC_CTL1, 0x00);  // unmute
  es8311_write(ES8311_DAC_MISC, 0x08);  // default
  es8311_write(ES8311_DAC_VOL,  0xBF);  // 0dB

  LOG_INFO("ES8311: OK");
  return true;
}

// --- I2S driver (TX: speaker via ES8311) ---
static bool i2s_init_tx(void) {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = true;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_BCK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DO;
  pins.data_in_num = I2S_DI;
  pins.mck_io_num = I2S_MCK;

  esp_err_t err;
  err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  if (err != ESP_OK) { LOG_ERR("I2S: install err %d", err); return false; }
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) { LOG_ERR("I2S: pin err %d", err); return false; }
  LOG_INFO("I2S TX: OK");
  return true;
}

// --- I2S driver (RX: microphone via INMP441 on I2S_NUM_1) ---
static volatile bool rx_recording = false;
static RingbufHandle_t rx_ringbuf = NULL;
static TaskHandle_t rx_task_handle = NULL;

#define RX_DMA_BUF_COUNT 4
#define RX_DMA_BUF_LEN   1024  // samples per DMA buffer
#define RX_CHUNK_SAMPLES 512   // samples per chunk (32ms at 16kHz)
#define RX_RINGBUF_SIZE  (RX_CHUNK_SAMPLES * sizeof(int16_t) * 8)  // 8 chunks

static void voice_rx_task(void *param) {
  int16_t chunk[RX_CHUNK_SAMPLES];

  while (rx_recording) {
    size_t bytes_read = 0;
    esp_err_t err = i2s_read(I2S_NUM_1, chunk, sizeof(chunk),
                             &bytes_read, pdMS_TO_TICKS(500));
    if (err == ESP_OK && bytes_read > 0) {
      // Write directly to ring buffer (no malloc/free)
      if (xRingbufferSend(rx_ringbuf, chunk, bytes_read, pdMS_TO_TICKS(50)) != pdTRUE) {
        // Ring buffer full, drop oldest by reading and discarding
        size_t item_size;
        void *item = xRingbufferReceive(rx_ringbuf, &item_size, 0);
        if (item) vRingbufferReturnItem(rx_ringbuf, item);
        xRingbufferSend(rx_ringbuf, chunk, bytes_read, 0);
      }
    }
  }
  rx_task_handle = NULL;
  vTaskDelete(NULL);
}

bool audio_init_rx(void) {
  // Configure INMP441 LRS pin as output LOW (left channel)
  pinMode(INMP441_LRS, OUTPUT);
  digitalWrite(INMP441_LRS, LOW);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = VOICE_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = RX_DMA_BUF_COUNT;
  cfg.dma_buf_len = RX_DMA_BUF_LEN;
  cfg.use_apll = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = INMP441_SCK;
  pins.ws_io_num = INMP441_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = INMP441_SD;
  pins.mck_io_num = I2S_PIN_NO_CHANGE;

  esp_err_t err;
  err = i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  if (err != ESP_OK) { LOG_ERR("I2S_RX: install err %d", err); return false; }
  err = i2s_set_pin(I2S_NUM_1, &pins);
  if (err != ESP_OK) { LOG_ERR("I2S_RX: pin err %d", err); return false; }
  i2s_stop(I2S_NUM_1);  // install but don't start yet

  rx_ringbuf = xRingbufferCreate(RX_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
  LOG_INFO("I2S_RX: INMP441 ready (ringbuf %d bytes)", RX_RINGBUF_SIZE);
  return true;
}

bool audio_start_recording(void) {
  if (rx_recording) return false;
  // Reset ring buffer
  if (rx_ringbuf) {
    vRingbufferDelete(rx_ringbuf);
    rx_ringbuf = xRingbufferCreate(RX_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
  }

  rx_recording = true;
  i2s_start(I2S_NUM_1);
  xTaskCreatePinnedToCore(voice_rx_task, "voice_rx", 4096, NULL, 3, &rx_task_handle, 1);
  LOG_INFO("Recording started");
  return true;
}

void audio_stop_recording(void) {
  rx_recording = false;
  i2s_stop(I2S_NUM_1);
  // Wait for task to exit
  int wait = 20;
  while (rx_task_handle && wait-- > 0) vTaskDelay(pdMS_TO_TICKS(10));
  LOG_INFO("Recording stopped");
}

int audio_read_chunk(int16_t *buf, int max_samples) {
  size_t item_size = 0;
  void *item = xRingbufferReceiveUpTo(rx_ringbuf, &item_size,
                                       pdMS_TO_TICKS(200),
                                       max_samples * sizeof(int16_t));
  if (item && item_size > 0) {
    int samples = item_size / sizeof(int16_t);
    memcpy(buf, item, item_size);
    vRingbufferReturnItem(rx_ringbuf, item);
    return samples;
  }
  return 0;
}

bool audio_is_recording(void) { return rx_recording; }

// --- Public API ---
bool audio_init(void) {
  // Enable amplifier via PCA9557 first
  if (!pca9557_init()) {
    LOG_WARN("Audio: PCA9557 init FAILED, amp may be off");
  }

  if (!es8311_init()) {
    LOG_ERR("Audio: ES8311 init FAILED");
    return false;
  }
  if (!i2s_init_tx()) {
    LOG_ERR("Audio: I2S init FAILED");
    return false;
  }

  LOG_INFO("Audio: ready");
  LOG_INFO("Audio: beep...");
  audio_set_volume(100);
  audio_play_sine(500, 15000);
  return true;
}

bool audio_play_sine(int freq_hz, int duration_ms) {
  const int CHUNK = 1024;
  int16_t *buf = (int16_t *)malloc(CHUNK * sizeof(int16_t));
  if (!buf) return false;
  int total = (44100 * duration_ms) / 1000;
  int written_total = 0;
  while (written_total < total) {
    int n = (total - written_total > CHUNK) ? CHUNK : (total - written_total);
    for (int i = 0; i < n; i++)
      buf[i] = (int16_t)(sinf(2.0f * M_PI * freq_hz * (written_total + i) / 44100.0f) * 20000.0f);
    size_t written;
    i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, portMAX_DELAY);
    written_total += n;
  }
  free(buf);
  return true;
}

struct __attribute__((packed)) WavHeader {
  char riff[4]; uint32_t file_size; char wave[4];
  char fmt[4]; uint32_t fmt_len; uint16_t fmt_tag;
  uint16_t channels; uint32_t sample_rate; uint32_t byte_rate;
  uint16_t block_align; uint16_t bits_per_sample;
  char data[4]; uint32_t data_size;
};

static volatile bool playing = false;
static void play_wav_task(void *param);

bool audio_play_wav(const char *path) {
  if (playing) return false;
  char *copy = strdup(path);
  if (!copy) return false;
  playing = true;
  xTaskCreate(play_wav_task, "audio_play", 4096, copy, 1, NULL);
  return true;
}

static void play_wav_task(void *param) {
  char *path = (char *)param;
  File f = SD_MMC.open(path);
  if (!f) {
    LOG_ERR("Audio: cannot open %s", path);
    free(path); playing = false; vTaskDelete(NULL); return;
  }
  LOG_INFO("Playing: %s", path);
  free(path);

  WavHeader hdr;
  size_t nr = f.read((uint8_t*)&hdr, sizeof(hdr));
  if (nr != sizeof(hdr) || memcmp(hdr.riff, "RIFF", 4) != 0 || memcmp(hdr.wave, "WAVE", 4) != 0) {
    LOG_ERR("Audio: invalid WAV header");
    f.close(); playing = false; vTaskDelete(NULL); return;
  }

  i2s_set_sample_rates(I2S_NUM_0, hdr.sample_rate);
  uint8_t *buf = (uint8_t *)malloc(1024);
  if (!buf) {
    LOG_ERR("Audio: malloc failed for playback buffer");
    f.close(); playing = false; vTaskDelete(NULL); return;
  }

  while (playing && f.available()) {
    int n = f.read(buf, 1024);
    if (n <= 0) break;
    size_t written;
    i2s_write(I2S_NUM_0, buf, n, &written, portMAX_DELAY);
  }
  free(buf);
  f.close();
  i2s_set_sample_rates(I2S_NUM_0, 44100);  // restore default
  playing = false;
  LOG_INFO("Playback done");
  vTaskDelete(NULL);
}

void audio_stop(void) { playing = false; i2s_zero_dma_buffer(I2S_NUM_0); }
void audio_set_volume(uint8_t vol) {
  if (vol > 100) vol = 100;
  // DAC_VOL: 0x00=-95dB ... 0xBF=0dB ... 0xFF=+32dB
  uint8_t reg = (vol * 0xBF) / 100;
  if (reg > 0xBF) reg = 0xBF;
  if (reg < 0x01) reg = 0x01;
  es8311_write(ES8311_DAC_VOL, reg);
}
bool audio_is_playing(void) { return playing; }

// Play TTS audio received from phone (blocking, single call)
void audio_play_response(const int16_t *data, int samples, int sample_rate) {
  if (!data || samples <= 0) return;
  i2s_set_sample_rates(I2S_NUM_0, sample_rate);
  size_t written = 0;
  i2s_write(I2S_NUM_0, data, samples * sizeof(int16_t), &written, portMAX_DELAY);
  i2s_set_sample_rates(I2S_NUM_0, 44100);  // restore default
}
