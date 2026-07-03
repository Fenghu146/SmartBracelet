// IMA ADPCM encoder/decoder
// Reference: https://wiki.multimedia.cx/index.php/IMA_ADPCM
// 4:1 compression ratio (16-bit PCM → 4-bit per sample)

#include "adpcm.h"

// Quantization step size table (89 entries)
static const int16_t step_table[89] = {
       7,    8,    9,   10,   11,   12,   13,   14,   16,   17,
      19,   21,   23,   25,   28,   31,   34,   37,   41,   45,
      50,   55,   60,   66,   73,   80,   88,   97,  107,  118,
     130,  143,  157,  173,  190,  209,  230,  253,  279,  307,
     337,  371,  408,  449,  494,  544,  598,  658,  724,  796,
     876,  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767
};

// Index adjustment per nibble value (16 entries)
static const int8_t index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

uint8_t adpcm_encode_sample(int16_t sample, adpcm_state_t *state) {
    int16_t delta = sample - state->predictor;
    int step = step_table[state->index];
    int nibble = 0;

    if (delta < 0) {
        nibble = 8;
        delta = -delta;
    }

    // Quantize delta using the step size
    int diff = step >> 3;
    if (delta >= step) { nibble |= 4; delta -= step; diff += step; }
    step >>= 1;
    if (delta >= step) { nibble |= 2; delta -= step; diff += step >> 1; }
    step >>= 1;
    if (delta >= step) { nibble |= 1; diff += step >> 1; }

    // Clamp diff to prevent overflow in predictor
    if (diff > 32767) diff = 32767;

    // Update predictor
    if (nibble & 8) {
        state->predictor -= (int16_t)diff;
    } else {
        state->predictor += (int16_t)diff;
    }

    // Clamp predictor to 16-bit signed range
    if (state->predictor > 32767) state->predictor = 32767;
    if (state->predictor < -32768) state->predictor = -32768;

    // Update index
    state->index += index_table[nibble & 7];
    if (state->index > 88) state->index = 88;
    if (state->index < 0)  state->index = 0;

    return (uint8_t)(nibble & 0x0F);
}

int16_t adpcm_decode_sample(uint8_t nibble, adpcm_state_t *state) {
    int step = step_table[state->index];
    int delta = step >> 3;

    if (nibble & 4) delta += step;
    if (nibble & 2) delta += step >> 1;
    if (nibble & 1) delta += step >> 2;

    if (nibble & 8) {
        state->predictor -= (int16_t)delta;
    } else {
        state->predictor += (int16_t)delta;
    }

    if (state->predictor > 32767) state->predictor = 32767;
    if (state->predictor < -32768) state->predictor = -32768;

    state->index += index_table[nibble & 7];
    if (state->index > 88) state->index = 88;
    if (state->index < 0)  state->index = 0;

    return state->predictor;
}

int adpcm_encode_buf(const int16_t *pcm_in, int sample_count,
                     uint8_t *adpcm_out, adpcm_state_t *state) {
    if (!pcm_in || !adpcm_out || sample_count <= 0) return 0;

    int out_idx = 0;
    for (int i = 0; i + 1 < sample_count; i += 2) {
        uint8_t lo = adpcm_encode_sample(pcm_in[i], state);
        uint8_t hi = adpcm_encode_sample(pcm_in[i + 1], state);
        adpcm_out[out_idx++] = (hi << 4) | lo;
    }
    // If odd sample count, just encode the last one with lo nibble only
    if (sample_count & 1) {
        uint8_t lo = adpcm_encode_sample(pcm_in[sample_count - 1], state);
        adpcm_out[out_idx++] = (uint8_t)(0xF0 | lo);  // high nibble = 0xF (dummy)
    }
    return out_idx;
}

int adpcm_decode_buf(const uint8_t *adpcm_in, int byte_count,
                     int16_t *pcm_out, adpcm_state_t *state) {
    if (!adpcm_in || !pcm_out || byte_count <= 0) return 0;

    int out_idx = 0;
    for (int i = 0; i < byte_count; i++) {
        uint8_t byte = adpcm_in[i];
        pcm_out[out_idx++] = adpcm_decode_sample(byte & 0x0F, state);
        pcm_out[out_idx++] = adpcm_decode_sample((byte >> 4) & 0x0F, state);
    }
    return out_idx * 2;
}
