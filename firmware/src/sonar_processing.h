#ifndef SONAR_PROCESSING_H
#define SONAR_PROCESSING_H

#include "sonar_wav.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Convert one completed raw PDM DMA frame into one in-memory
 * 16-channel, 96 kHz, 16-bit PCM WAV payload.
 *
 * The returned buffer is owned by this module and remains valid until
 * the next call. Call this from only one task at a time.
 */
bool sonar_processing_make_wav(const uint8_t *pdm,
                               uint32_t pdm_bytes,
                               uint8_t **wav_data,
                               uint32_t *wav_bytes,
                               uint32_t *pcm_frames);

#endif
