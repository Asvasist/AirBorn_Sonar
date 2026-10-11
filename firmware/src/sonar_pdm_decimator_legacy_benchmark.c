#include "sonar_pdm_decimator_legacy_benchmark.h"

#include <stddef.h>
#include <stdint.h>

#include "sonar_rx_fir_legacy47.h"

/* The output sample at PDM index c is sum(h[t] * s[c - FIR_HALF + t]) over the
 * FIR_TAPS taps, with s = +1/-1. Because s is one bit, eight consecutive taps
 * collapse into one table entry indexed by eight PDM bits: 641 lookup groups per
 * output sample instead of 5121 direct taps. The sum is unchanged, so
 * the result is bit-identical to sonar_pdm_legacy_reference_sample(). */
#define LUT_GROUPS    ((FIR_TAPS + 7U) / 8U)
#define CHANNEL_WORDS ((SONAR_PDM_SAMPLES / 32U) + 2U) /* +2: zero padding read by window() */

static int32_t lut[LUT_GROUPS][256];
static bool lut_ready;
static uint32_t channel_bits[CHANNEL_WORDS];

static int32_t tap(uint32_t t)
{
    if (t >= FIR_TAPS) {
        return 0;
    }
    return fir_q31[t > FIR_HALF ? t - FIR_HALF : FIR_HALF - t];
}

static void build_lut(void)
{
    for (uint32_t group = 0U; group < LUT_GROUPS; ++group) {
        for (uint32_t bits = 0U; bits < 256U; ++bits) {
            int32_t sum = 0;
            for (uint32_t k = 0U; k < 8U; ++k) {
                int32_t h = tap(group * 8U + k);
                sum += ((bits >> k) & 1U) != 0U ? h : -h;
            }
            lut[group][bits] = sum;
        }
    }
    lut_ready = true;
}

static uint32_t instant(const uint32_t *words, uint32_t index)
{
    uint32_t word = words[index >> 1U];
    return (index & 1U) != 0U ? word >> 16U : word & UINT32_C(0xffff);
}

/* Eight PDM bits of the current channel starting at bit position p. */
static uint32_t window(uint32_t p)
{
    uint64_t pair = ((uint64_t)channel_bits[(p >> 5U) + 1U] << 32U) | channel_bits[p >> 5U];
    return (uint32_t)(pair >> (p & 31U)) & 0xffU;
}

static int16_t q31_to_i16(int64_t value)
{
    int64_t scaled;

    if (value >= 0) {
        scaled = (value + INT64_C(32768)) / INT64_C(65536);
    } else {
        scaled = -(((-value) + INT64_C(32768)) / INT64_C(65536));
    }

    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

int16_t sonar_pdm_legacy_reference_sample(const uint32_t *words, uint32_t pdm_samples,
                                          uint32_t frame, unsigned channel)
{
    int64_t center = (int64_t)frame * SONAR_PDM_DECIMATION;
    int64_t acc = 0;

    for (uint32_t t = 0U; t < FIR_TAPS; ++t) {
        int64_t index = center - (int64_t)FIR_HALF + (int64_t)t;
        if (index >= 0 && index < (int64_t)pdm_samples) {
            /* Samples outside the capture are zero padding. */
            int32_t h = tap(t);
            acc += ((instant(words, (uint32_t)index) >> channel) & 1U) != 0U ? h : -h;
        }
    }
    return q31_to_i16(acc);
}

bool sonar_pdm_legacy_to_pcm16_interleaved(const uint8_t *pdm, uint32_t pdm_bytes, int16_t *pcm,
                                           uint32_t pcm_sample_capacity, uint32_t *pcm_frames_out)
{
    if (pdm == NULL || pcm == NULL || pcm_frames_out == NULL ||
        (((uintptr_t)pdm) & (sizeof(uint32_t) - 1U)) != 0U || (pdm_bytes & 3U) != 0U) {
        return false;
    }

    const uint32_t pdm_samples = (pdm_bytes / 4U) * 2U;
    const uint32_t pcm_frames = pdm_samples / SONAR_PDM_DECIMATION;

    if (pdm_samples > SONAR_PDM_SAMPLES || (pdm_samples % SONAR_PDM_DECIMATION) != 0U ||
        pcm_sample_capacity < pcm_frames * SONAR_ACQ_CHANNELS) {
        return false;
    }
    if (!lut_ready) {
        build_lut();
    }

    const uint32_t *words = (const uint32_t *)(const void *)pdm;

    for (unsigned channel = 0U; channel < SONAR_ACQ_CHANNELS; ++channel) {
        /* Transpose this channel into a contiguous bit stream. */
        for (uint32_t i = 0U; i < CHANNEL_WORDS; ++i) {
            channel_bits[i] = 0U;
        }
        for (uint32_t i = 0U; i < pdm_samples; ++i) {
            channel_bits[i >> 5U] |= ((instant(words, i) >> channel) & 1U) << (i & 31U);
        }
        for (uint32_t frame = 0U; frame < pcm_frames; ++frame) {
            uint32_t center = frame * SONAR_PDM_DECIMATION;
            int16_t sample;
            if (center < FIR_HALF || center + FIR_HALF >= pdm_samples) {
                sample = sonar_pdm_legacy_reference_sample(words, pdm_samples, frame, channel);
            } else {
                uint32_t start = center - FIR_HALF;
                int64_t acc = 0;
                for (uint32_t group = 0U; group < LUT_GROUPS; ++group) {
                    acc += lut[group][window(start + group * 8U)];
                }
                sample = q31_to_i16(acc);
            }
            pcm[frame * SONAR_ACQ_CHANNELS + channel] = sample;
        }
    }

    *pcm_frames_out = pcm_frames;
    return true;
}

uint32_t sonar_pdm_legacy_workspace_bytes(void)
{
    return (uint32_t)(sizeof(lut) + sizeof(channel_bits));
}

bool sonar_pdm_legacy_is_initialized(void)
{
    return lut_ready;
}
