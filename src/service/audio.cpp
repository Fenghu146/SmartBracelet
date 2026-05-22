// ES8311 audio codec + PCA9557 I2C I/O expander (PA_EN control)
// Pin assignments verified against Waveshare ESP32-S3-Touch-LCD-1.83 schematic

#include "audio.h"
#include "pin_config.h"
#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <driver/i2s.h>
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
    USBSerial.println("PCA9557: no response");
    return false;
  }
  // Set initial output: LCD_CS=1(high), PA_EN=1(high), DVP_PWDN=1(high)
  pca9557_write(PCA9557_OUTPUT, 0x07);
  USBSerial.println("PCA9557: PA_EN enabled");
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
#define ES8311_SDP_IN   0x09  // serial data port input (I2S format)
#define ES8311_SDP_OUT  0x0A  // serial data port output
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
    USBSerial.println("ES8311: I2C no response");
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
  es8311_write(ES8311_OUT, 0x00);   // HPSW=0 → line output mode

  // 7. Start state machine
  es8311_write(ES8311_RESET, 0x80); // CSM_ON=1 (slave mode)

  // 8. Unmute + set volume
  es8311_write(ES8311_DAC_CTL1, 0x00);  // unmute
  es8311_write(ES8311_DAC_MISC, 0x08);  // default
  es8311_write(ES8311_DAC_VOL,  0xBF);  // 0dB

  USBSerial.println("ES8311: OK");
  return true;
}

// --- I2S driver ---
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

// --- Public API ---
bool audio_init(void) {
  // Enable amplifier via PCA9557 first
  if (!pca9557_init()) {
    USBSerial.println("Audio: PCA9557 init FAILED, amp may be off");
  }

  if (!es8311_init()) {
    USBSerial.println("Audio: ES8311 init FAILED");
    return false;
  }
  if (!i2s_init_tx()) {
    USBSerial.println("Audio: I2S init FAILED");
    return false;
  }

  USBSerial.println("Audio: ready");
  USBSerial.println("Audio: beep...");
  audio_set_volume(100);
  audio_play_sine(500, 15000);
  return true;
}

bool audio_play_sine(int freq_hz, int duration_ms) {
  int total = (44100 * duration_ms) / 1000;
  int16_t *buf = (int16_t *)malloc(total * sizeof(int16_t));
  if (!buf) return false;
  for (int i = 0; i < total; i++)
    buf[i] = (int16_t)(sinf(2.0f * M_PI * freq_hz * i / 44100.0f) * 20000.0f);
  size_t written;
  i2s_write(I2S_NUM_0, buf, total * sizeof(int16_t), &written, portMAX_DELAY);
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
  if (!f) { free(path); playing = false; vTaskDelete(NULL); return; }
  USBSerial.printf("Playing: %s\n", path);
  free(path);

  WavHeader hdr;
  size_t nr = f.read((uint8_t*)&hdr, sizeof(hdr));
  if (nr != sizeof(hdr) || memcmp(hdr.riff, "RIFF", 4) != 0 || memcmp(hdr.wave, "WAVE", 4) != 0) {
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
