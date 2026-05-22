#include "audio.h"
#include "pin_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <FS.h>
#include <SD_MMC.h>

// ES8311 registers
#define ES8311_RESET     0x00
#define ES8311_CLK_MAN   0x01
#define ES8311_CLK_AD1   0x02
#define ES8311_CLK_AD2   0x03
#define ES8311_DAC_PWR   0x04
#define ES8311_DAC_PWR2  0x05
#define ES8311_ADC_PWR   0x06
#define ES8311_MIC1_PWR  0x07
#define ES8311_MIC2_PWR  0x08
#define ES8311_DAC_CTL   0x09
#define ES8311_ADC_CTL   0x0A
#define ES8311_DAC_VOL   0x0B
#define ES8311_ADC_VOL   0x0C
#define ES8311_DAC_SEL   0x16
#define ES8311_ADC_SEL   0x17
#define ES8311_DAC_MUTE  0x18
#define ES8311_GPIO_EN   0x1B

static bool es8311_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool es8311_init(void) {
  delay(10);
  if (!es8311_write(ES8311_RESET, 0x1F)) {
    USBSerial.println("ES8311: I2C not responding");
    return false;
  }
  delay(10);
  es8311_write(ES8311_RESET, 0x00);
  delay(50);

  es8311_write(ES8311_CLK_MAN,  0x48);
  es8311_write(ES8311_CLK_AD1,  0x00);
  es8311_write(ES8311_CLK_AD2,  0x24);
  es8311_write(ES8311_DAC_PWR,  0x00);
  es8311_write(ES8311_DAC_PWR2, 0x08);
  es8311_write(ES8311_ADC_PWR,  0x00);
  es8311_write(ES8311_DAC_CTL,  0x08);  // I2S, 16-bit
  es8311_write(ES8311_DAC_SEL,  0x1A);  // DAC from I2S
  es8311_write(ES8311_ADC_SEL,  0xA0);
  es8311_write(ES8311_DAC_MUTE, 0x00);  // unmute
  es8311_write(ES8311_DAC_VOL,  0x28);  // ~80% vol

  USBSerial.println("ES8311: OK");
  return true;
}

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
  if (err != ESP_OK) { USBSerial.printf("I2S: install err %d\n", err); return false; }
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) { USBSerial.printf("I2S: pin err %d\n", err); return false; }
  USBSerial.println("I2S: OK");
  return true;
}

bool audio_init(void) {
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, LOW);
  if (!es8311_init()) return false;
  if (!i2s_init_tx()) return false;
  digitalWrite(PA_EN, HIGH);
  USBSerial.println("Audio: ready");
  return true;
}

// WAV header
struct __attribute__((packed)) WavHeader {
  char riff[4];
  uint32_t file_size;
  char wave[4];
  char fmt[4];
  uint32_t fmt_len;
  uint16_t fmt_tag;
  uint16_t channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t block_align;
  uint16_t bits_per_sample;
  char data[4];
  uint32_t data_size;
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
    USBSerial.printf("Audio: can't open %s\n", path);
    free(path); playing = false; vTaskDelete(NULL); return;
  }
  USBSerial.printf("Playing: %s\n", path);
  free(path);  // path no longer needed

  WavHeader hdr;
  size_t nr = f.read((uint8_t*)&hdr, sizeof(hdr));
  if (nr != sizeof(hdr) || memcmp(hdr.riff, "RIFF", 4) != 0 ||
      memcmp(hdr.wave, "WAVE", 4) != 0) {
    USBSerial.println("Audio: invalid WAV");
    f.close(); playing = false; vTaskDelete(NULL); return;
  }

  i2s_set_sample_rates(I2S_NUM_0, hdr.sample_rate);
  uint8_t *buf = (uint8_t *)malloc(1024);
  if (!buf) { f.close(); playing = false; vTaskDelete(NULL); return; }

  while (playing && f.available()) {
    int n = f.read(buf, 1024);
    if (n <= 0) break;
    size_t written;
    i2s_write(I2S_NUM_0, buf, n, &written, portMAX_DELAY);
  }

  free(buf); f.close();
  playing = false;
  USBSerial.println("Playback done");
  vTaskDelete(NULL);
}

void audio_stop(void) {
  playing = false;
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void audio_set_volume(uint8_t vol) {
  if (vol > 100) vol = 100;
  es8311_write(ES8311_DAC_VOL, (vol * 0x30) / 100);
}

bool audio_is_playing(void) { return playing; }
