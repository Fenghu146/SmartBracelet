#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// IMA ADPCM state (16 bytes)
typedef struct {
    int16_t predictor;  // predicted sample value
    int8_t  index;      // step size table index (0-88, clamped)
} adpcm_state_t;

// Reset encoder/decoder state to initial conditions
static inline void adpcm_reset(adpcm_state_t *state) {
    state->predictor = 0;
    state->index = 0;
}

// Encode one 16-bit PCM sample to a 4-bit ADPCM nibble.
// Returns the nibble (lower 4 bits). Updates *state.
uint8_t adpcm_encode_sample(int16_t sample, adpcm_state_t *state);

// Decode one 4-bit ADPCM nibble back to 16-bit PCM.
// Returns the decoded sample. Updates *state.
int16_t adpcm_decode_sample(uint8_t nibble, adpcm_state_t *state);

// Encode a buffer of PCM samples to ADPCM bytes (4:1 compression).
// Each pair of nibbles becomes one byte: low nibble = sample N, high nibble = sample N+1.
// pcm_in: input PCM samples (sample_count must be even)
// sample_count: number of input samples
// adpcm_out: output buffer (sample_count / 2 bytes minimum)
// Returns number of bytes written to adpcm_out.
int adpcm_encode_buf(const int16_t *pcm_in, int sample_count,
                     uint8_t *adpcm_out, adpcm_state_t *state);

// Decode ADPCM bytes back to PCM samples.
// adpcm_in: input ADPCM bytes
// byte_count: number of input bytes
// pcm_out: output PCM buffer (byte_count * 2 samples minimum)
// Returns number of samples written.
int adpcm_decode_buf(const uint8_t *adpcm_in, int byte_count,
                     int16_t *pcm_out, adpcm_state_t *state);

#ifdef __cplusplus
}
#endif
