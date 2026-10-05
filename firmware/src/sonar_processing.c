#include "sonar_processing.h"

#include <stddef.h>
#include <stdint.h>

/*
 * One working WAV buffer is enough for the current Ethernet sender because
 * send_record() processes and sends one frame at a time.
 */
static uint8_t wav_buffer[SONAR_WAV_TOTAL_BYTES] __attribute__((aligned(32)));

bool sonar_processing_make_wav(const uint8_t *pdm,
                               uint32_t pdm_bytes,
                               uint8_t **wav_data,
                               uint32_t *wav_bytes,
                               uint32_t *pcm_frames)
{
    if (pdm == NULL || wav_data == NULL || wav_bytes == NULL ||
        pcm_frames == NULL) {
        return false;
    }

    int16_t *pcm =
        (int16_t *)(void *)(wav_buffer + SONAR_WAV_HEADER_BYTES);

    const uint32_t pcm_capacity =
        (SONAR_WAV_TOTAL_BYTES - SONAR_WAV_HEADER_BYTES) /
        sizeof(int16_t);

    uint32_t frames = 0U;

    if (!sonar_pdm_to_pcm16_interleaved(pdm,
                                        pdm_bytes,
                                        pcm,
                                        pcm_capacity,
                                        &frames)) {
        return false;
    }

    const uint32_t total = sonar_wav_total_bytes(frames);

    if (total > sizeof(wav_buffer) ||
        !sonar_wav_write_header(wav_buffer, frames)) {
        return false;
    }

    *wav_data = wav_buffer;
    *wav_bytes = total;
    *pcm_frames = frames;
    return true;
}
