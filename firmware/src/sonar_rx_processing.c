#include "sonar_rx_processing.h"
#include "sonar_clock.h"
#include "sonar_console.h"
#include "xil_printf.h"

#ifndef SONAR_RX_DSP_TIMING
#define SONAR_RX_DSP_TIMING 1
#endif
#ifndef SONAR_RX_COMPARE_LEGACY
#define SONAR_RX_COMPARE_LEGACY 0
#endif
#if SONAR_RX_COMPARE_LEGACY
#include "sonar_pdm_decimator_legacy_benchmark.h"
static int16_t legacy_pcm[SONAR_PCM_SAMPLES] __attribute__((aligned(32)));
#endif

#include <stddef.h>
#include <stdint.h>

/*
 * One working WAV buffer is enough for the current Ethernet sender because
 * send_record() processes and sends one frame at a time.
 */
static uint8_t wav_buffer[SONAR_WAV_TOTAL_BYTES] __attribute__((aligned(32)));

bool sonar_processing_make_wav(const uint8_t *pdm, uint32_t pdm_bytes, uint8_t **wav_data,
                               uint32_t *wav_bytes, uint32_t *pcm_frames)
{
    if (pdm == NULL || wav_data == NULL || wav_bytes == NULL || pcm_frames == NULL) {
        return false;
    }

    int16_t *pcm = (int16_t *)(void *)(wav_buffer + SONAR_WAV_HEADER_BYTES);

    const uint32_t pcm_capacity =
        (SONAR_WAV_TOTAL_BYTES - SONAR_WAV_HEADER_BYTES) / sizeof(int16_t);

    uint32_t frames = 0U;

#if SONAR_RX_COMPARE_LEGACY
    uint32_t legacy_frames = 0U;
    const bool legacy_cold = !sonar_pdm_legacy_is_initialized();
    const uint64_t legacy_began = sonar_clock_us();
    const bool legacy_ok = sonar_pdm_legacy_to_pcm16_interleaved(pdm, pdm_bytes, legacy_pcm,
                                                                 SONAR_PCM_SAMPLES, &legacy_frames);
    const uint64_t legacy_elapsed = sonar_clock_us() - legacy_began;
#endif
#if SONAR_RX_DSP_TIMING || SONAR_RX_COMPARE_LEGACY
    const bool cold = !sonar_pdm_is_initialized();
    const uint64_t began = sonar_clock_us();
#endif
    const bool converted =
        sonar_pdm_to_pcm16_interleaved(pdm, pdm_bytes, pcm, pcm_capacity, &frames);
#if SONAR_RX_DSP_TIMING || SONAR_RX_COMPARE_LEGACY
    const uint64_t elapsed = sonar_clock_us() - began;
    /* Take timestamps before logging. Elapsed time includes task preemption. */
    sonar_console_lock();
    xil_printf("DSP 2stage: %u ms + %u us; cold=%u ok=%u frames=%u workspace=%u bytes\r\n",
               (unsigned)(elapsed / 1000U), (unsigned)(elapsed % 1000U), (unsigned)cold,
               (unsigned)converted, (unsigned)frames, (unsigned)sonar_pdm_workspace_bytes());
#if SONAR_RX_COMPARE_LEGACY
    xil_printf("DSP legacy47: %u ms + %u us; cold=%u ok=%u frames=%u workspace=%u bytes\r\n",
               (unsigned)(legacy_elapsed / 1000U), (unsigned)(legacy_elapsed % 1000U),
               (unsigned)legacy_cold, (unsigned)legacy_ok, (unsigned)legacy_frames,
               (unsigned)sonar_pdm_legacy_workspace_bytes());
#endif
    sonar_console_unlock();
#endif
    if (!converted) {
        return false;
    }
#if SONAR_RX_COMPARE_LEGACY
    if (!legacy_ok || legacy_frames != frames) {
        return false;
    }
#endif

    const uint32_t total = sonar_wav_total_bytes(frames);

    if (total > sizeof(wav_buffer) || !sonar_wav_write_header(wav_buffer, frames)) {
        return false;
    }

    *wav_data = wav_buffer;
    *wav_bytes = total;
    *pcm_frames = frames;
    return true;
}
