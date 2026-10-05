#include "sonar_component_selftest.h"
#include "sonar_pdm.h"
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { return false; } } while (0)

static uint32_t pdm[SONAR_STAGE2_WORDS];
static int16_t pcm[SONAR_PCM_SAMPLES];

static void fill_random(uint32_t seed)
{
    for (uint32_t i = 0U; i < SONAR_STAGE2_WORDS; ++i) {
        seed = seed * 1664525U + 1013904223U;
        pdm[i] = seed;
    }
}

static bool convert(uint32_t *frames)
{
    return sonar_pdm_to_pcm16_interleaved((const uint8_t *)pdm, sizeof(pdm), pcm,
                                          SONAR_PCM_SAMPLES, frames);
}

/* Every output sample, including the zero-padded edges, matches the direct form. */
static bool fast_path_matches_reference(void)
{
    uint32_t frames = 0U;
    fill_random(12345U);
    CHECK(convert(&frames) && frames == SONAR_PCM_FRAMES);
    for (uint32_t frame = 0U; frame < frames; ++frame) {
        for (unsigned channel = 0U; channel < SONAR_STAGE2_CHANNELS; ++channel) {
            int16_t expected = sonar_pdm_reference_sample(pdm, SONAR_PDM_SAMPLES, frame, channel);
            CHECK(pcm[frame * SONAR_STAGE2_CHANNELS + channel] == expected);
        }
    }
    return true;
}

/* Constant input passes at unity DC gain (Q31 sum of taps is exactly 1.0). */
static bool constant_input_is_full_scale(void)
{
    uint32_t frames = 0U;
    const uint32_t middle = (SONAR_PCM_FRAMES / 2U) * SONAR_STAGE2_CHANNELS;
    memset(pdm, 0xff, sizeof(pdm));
    CHECK(convert(&frames));
    CHECK(pcm[middle] == INT16_MAX && pcm[middle + 15U] == INT16_MAX);
    memset(pdm, 0x00, sizeof(pdm));
    CHECK(convert(&frames));
    return pcm[middle] == INT16_MIN && pcm[middle + 7U] == INT16_MIN;
}

/* Channel c is bit c of each 16-bit instant; channels must not leak into each other. */
static bool channels_are_independent(void)
{
    uint32_t frames = 0U;
    const uint32_t middle = (SONAR_PCM_FRAMES / 2U) * SONAR_STAGE2_CHANNELS;
    for (uint32_t i = 0U; i < SONAR_STAGE2_WORDS; ++i) { pdm[i] = 0x00200020U; } /* ch 5 high */
    CHECK(convert(&frames));
    for (unsigned channel = 0U; channel < SONAR_STAGE2_CHANNELS; ++channel) {
        CHECK(pcm[middle + channel] == (channel == 5U ? INT16_MAX : INT16_MIN));
    }
    return true;
}

static bool invalid_arguments_rejected(void)
{
    uint32_t frames = 77U;
    const uint8_t *bytes = (const uint8_t *)pdm;
    CHECK(!sonar_pdm_to_pcm16_interleaved(NULL, sizeof(pdm), pcm, SONAR_PCM_SAMPLES, &frames));
    CHECK(!sonar_pdm_to_pcm16_interleaved(bytes + 1, 64U, pcm, SONAR_PCM_SAMPLES, &frames));
    CHECK(!sonar_pdm_to_pcm16_interleaved(bytes, 6U, pcm, SONAR_PCM_SAMPLES, &frames));
    CHECK(!sonar_pdm_to_pcm16_interleaved(bytes, sizeof(pdm), pcm, SONAR_PCM_SAMPLES - 1U, &frames));
    CHECK(!sonar_pdm_to_pcm16_interleaved(bytes, sizeof(pdm) + 400U, pcm, UINT32_MAX, &frames));
    return frames == 77U;
}

sonar_test_result_t sonar_pdm_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"pdm fast path matches direct-form reference", fast_path_matches_reference},
        {"pdm constant input is full scale", constant_input_is_full_scale},
        {"pdm channels are independent", channels_are_independent},
        {"pdm invalid arguments rejected", invalid_arguments_rejected},
    };
    sonar_test_result_t result = {0U, 0U};
    for (size_t i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        report(tests[i].name, passed);
        if (passed) { ++result.passed; } else { ++result.failed; }
    }
    return result;
}
