#include "sonar_pdm_decimator.h"
#include "sonar_rx_fir_stages.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define LUT_GROUPS        ((SONAR_RX_STAGE1_TAPS + 7U) / 8U)
#define CHANNEL_WORDS     ((SONAR_PDM_SAMPLES / 32U) + 2U)
#define STAGE1_MARGIN     ((SONAR_RX_STAGE1_HALF + 4U) / 5U)
#define STAGE1_CAPACITY   ((SONAR_PDM_SAMPLES / 5U) + 2U * STAGE1_MARGIN)
#define ACCUMULATOR_SHIFT (SONAR_RX_STAGE1_Q + SONAR_RX_STAGE2_Q - 15U)
#define PCM_DIVISOR       (INT64_C(1) << ACCUMULATOR_SHIFT)
#define PCM_ROUND_HALF    (PCM_DIVISOR / 2)

/* One conversion owner (the Ethernet task), with channel-local buffers reused
 * for all 16 channels. No heap allocation and no stack-sized capture arrays. */
static int32_t lut[LUT_GROUPS][256];
static uint32_t channel_bits[CHANNEL_WORDS];
static int32_t intermediate[STAGE1_CAPACITY];
static bool lut_ready;

_Static_assert(SONAR_RX_STAGE1_ABS_SUM <= INT32_MAX / 2,
               "Stage-one sums and paired samples must fit signed 32 bits");
_Static_assert(SONAR_RX_ACCUMULATOR_BOUND <= INT64_MAX - PCM_ROUND_HALF,
               "Stage-two accumulation and rounding must fit signed 64 bits");
_Static_assert(SONAR_PDM_SAMPLES < INT32_MAX - 2U * SONAR_RX_STAGE1_HALF,
               "Signed boundary indices must fit");
_Static_assert(SONAR_PDM_DECIMATION == 25U, "This pipeline is 5 x 5 decimation");

static int32_t stage1_tap(uint32_t index)
{
    if (index >= SONAR_RX_STAGE1_TAPS) {
        return 0;
    }
    const uint32_t distance =
        index > SONAR_RX_STAGE1_HALF ? index - SONAR_RX_STAGE1_HALF : SONAR_RX_STAGE1_HALF - index;
    return sonar_rx_stage1_q28[distance];
}

static void build_lut(void)
{
    for (uint32_t group = 0U; group < LUT_GROUPS; ++group) {
        for (uint32_t bits = 0U; bits < 256U; ++bits) {
            int32_t sum = 0;
            for (uint32_t k = 0U; k < 8U; ++k) {
                const int32_t coefficient = stage1_tap(group * 8U + k);
                sum += ((bits >> k) & 1U) != 0U ? coefficient : -coefficient;
            }
            lut[group][bits] = sum;
        }
    }
    lut_ready = true;
}

static uint32_t instant(const uint32_t *words, uint32_t index)
{
    const uint32_t word = words[index >> 1U];
    return (index & 1U) != 0U ? word >> 16U : word & UINT32_C(0xffff);
}

static uint32_t window(uint32_t bit_index)
{
    const uint64_t pair =
        ((uint64_t)channel_bits[(bit_index >> 5U) + 1U] << 32U) | channel_bits[bit_index >> 5U];
    return (uint32_t)(pair >> (bit_index & 31U)) & UINT32_C(0xff);
}

static int32_t stage1_sample(const uint32_t *words, uint32_t samples, int32_t center,
                             unsigned channel)
{
    int32_t sum = 0;
    if (center >= (int32_t)SONAR_RX_STAGE1_HALF &&
        (uint32_t)center + SONAR_RX_STAGE1_HALF < samples) {
        const uint32_t start = (uint32_t)center - SONAR_RX_STAGE1_HALF;
        for (uint32_t group = 0U; group < LUT_GROUPS; ++group) {
            sum += lut[group][window(start + 8U * group)];
        }
    } else {
        for (uint32_t t = 0U; t < SONAR_RX_STAGE1_TAPS; ++t) {
            const int32_t index = center - (int32_t)SONAR_RX_STAGE1_HALF + (int32_t)t;
            if (index >= 0 && (uint32_t)index < samples) {
                const int32_t coefficient = stage1_tap(t);
                sum += ((instant(words, (uint32_t)index) >> channel) & 1U) != 0U ? coefficient
                                                                                 : -coefficient;
            }
        }
    }
    return sum;
}

static int32_t intermediate_at(int32_t index, uint32_t count)
{
    if (index < 0 || (uint32_t)index >= count) {
        return 0;
    }
    return intermediate[(uint32_t)index];
}

static int16_t accumulator_to_pcm16(int64_t value)
{
    const int64_t scaled = value >= 0 ? (value + PCM_ROUND_HALF) / PCM_DIVISOR
                                      : -((-value + PCM_ROUND_HALF) / PCM_DIVISOR);
    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static int16_t stage2_sample(uint32_t center, uint32_t count)
{
    int64_t sum = (int64_t)intermediate[center] * sonar_rx_stage2_q31[0];
    if (center >= SONAR_RX_STAGE2_HALF && center + SONAR_RX_STAGE2_HALF < count) {
        for (uint32_t distance = 1U; distance <= SONAR_RX_STAGE2_HALF; ++distance) {
            /* The generated L1 bound proves this signed 32-bit addition safe. */
            const int32_t pair = intermediate[center - distance] + intermediate[center + distance];
            sum += (int64_t)pair * sonar_rx_stage2_q31[distance];
        }
    } else {
        for (uint32_t distance = 1U; distance <= SONAR_RX_STAGE2_HALF; ++distance) {
            const int32_t pair = intermediate_at((int32_t)center - (int32_t)distance, count) +
                                 intermediate_at((int32_t)center + (int32_t)distance, count);
            sum += (int64_t)pair * sonar_rx_stage2_q31[distance];
        }
    }
    return accumulator_to_pcm16(sum);
}

uint32_t sonar_pdm_workspace_bytes(void)
{
    return (uint32_t)(sizeof(lut) + sizeof(channel_bits) + sizeof(intermediate));
}

bool sonar_pdm_is_initialized(void)
{
    return lut_ready;
}

static bool spans_overlap(uintptr_t a, uint32_t a_bytes, uintptr_t b, uint32_t b_bytes)
{
    return a <= b ? b - a < a_bytes : a - b < b_bytes;
}

bool sonar_pdm_to_pcm16_interleaved(const uint8_t *pdm, uint32_t pdm_bytes, int16_t *pcm,
                                    uint32_t pcm_sample_capacity, uint32_t *pcm_frames_out)
{
    if (pdm == NULL || pcm == NULL || pcm_frames_out == NULL || pdm_bytes == 0U ||
        ((uintptr_t)pdm % sizeof(uint32_t)) != 0U || ((uintptr_t)pcm % _Alignof(int16_t)) != 0U ||
        ((uintptr_t)pcm_frames_out % _Alignof(uint32_t)) != 0U || (pdm_bytes & 3U) != 0U ||
        pdm_bytes > SONAR_ACQ_BYTES) {
        return false;
    }
    const uint32_t samples = (pdm_bytes / 4U) * 2U;
    const uint32_t frames = samples / SONAR_PDM_DECIMATION;
    if (samples % SONAR_PDM_DECIMATION != 0U || pcm_sample_capacity < frames * SONAR_ACQ_CHANNELS) {
        return false;
    }
    const uint32_t pcm_bytes = frames * SONAR_ACQ_CHANNELS * (uint32_t)sizeof(int16_t);
    if (spans_overlap((uintptr_t)pdm, pdm_bytes, (uintptr_t)pcm, pcm_bytes) ||
        spans_overlap((uintptr_t)pdm, pdm_bytes, (uintptr_t)pcm_frames_out,
                      (uint32_t)sizeof(*pcm_frames_out)) ||
        spans_overlap((uintptr_t)pcm, pcm_bytes, (uintptr_t)pcm_frames_out,
                      (uint32_t)sizeof(*pcm_frames_out))) {
        return false;
    }
    if (!lut_ready) {
        build_lut();
    }
    const uint32_t *words = (const uint32_t *)(const void *)pdm;
    const uint32_t first_stage_count = samples / 5U;
    const uint32_t padded_count = first_stage_count + 2U * STAGE1_MARGIN;
    for (unsigned channel = 0U; channel < SONAR_ACQ_CHANNELS; ++channel) {
        for (uint32_t i = 0U; i < CHANNEL_WORDS; ++i) {
            channel_bits[i] = 0U;
        }
        for (uint32_t i = 0U; i < samples; ++i) {
            channel_bits[i >> 5U] |= ((instant(words, i) >> channel) & 1U) << (i & 31U);
        }
        /* Retain first-stage tails outside the capture. Simply zero-padding
         * the intermediate buffer would give an incorrect cascade at edges. */
        for (uint32_t i = 0U; i < padded_count; ++i) {
            const int32_t center = ((int32_t)i - (int32_t)STAGE1_MARGIN) * 5;
            intermediate[i] = stage1_sample(words, samples, center, channel);
        }
        for (uint32_t frame = 0U; frame < frames; ++frame) {
            pcm[frame * SONAR_ACQ_CHANNELS + channel] =
                stage2_sample(STAGE1_MARGIN + frame * 5U, padded_count);
        }
    }
    *pcm_frames_out = frames;
    return true;
}

#if SONAR_PDM_TEST_REFERENCE
#include "sonar_rx_fir_reference.h"
/* Independent, integer direct-form cascade impulse response. Test builds only;
 * no reference coefficient array is included in the normal target image. */
int16_t sonar_pdm_reference_sample(const uint32_t *words, uint32_t samples, uint32_t frame,
                                   unsigned channel)
{
    if (words == NULL || samples == 0U || samples > SONAR_PDM_SAMPLES ||
        channel >= SONAR_ACQ_CHANNELS || frame >= samples / SONAR_PDM_DECIMATION) {
        return 0;
    }
    const int32_t center = (int32_t)(frame * SONAR_PDM_DECIMATION);
    int64_t sum = 0;
    for (uint32_t t = 0U; t < SONAR_RX_EFFECTIVE_TAPS; ++t) {
        const int32_t index = center - (int32_t)SONAR_RX_EFFECTIVE_HALF + (int32_t)t;
        if (index >= 0 && (uint32_t)index < samples) {
            const uint32_t distance = t > SONAR_RX_EFFECTIVE_HALF ? t - SONAR_RX_EFFECTIVE_HALF
                                                                  : SONAR_RX_EFFECTIVE_HALF - t;
            const int64_t coefficient = sonar_rx_reference_q59[distance];
            sum += ((instant(words, (uint32_t)index) >> channel) & 1U) != 0U ? coefficient
                                                                             : -coefficient;
        }
    }
    return accumulator_to_pcm16(sum);
}
#endif
