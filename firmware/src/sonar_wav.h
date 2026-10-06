#ifndef SONAR_WAV_H
#define SONAR_WAV_H

#include "sonar_pdm.h"
#include <stdbool.h>
#include <stdint.h>

#define SONAR_WAV_HEADER_BYTES 44U
#define SONAR_WAV_DATA_BYTES   SONAR_PCM_BYTES
#define SONAR_WAV_TOTAL_BYTES  (SONAR_WAV_HEADER_BYTES + SONAR_WAV_DATA_BYTES)

/* Optional bit in the existing sonar metadata flags to identify WAV payloads. */
#define SONAR_FRAME_FLAG_WAV_PCM16 UINT32_C(0x00000100)

/*
 * Write a standard RIFF/WAVE PCM header.
 * The PCM data must immediately follow the 44-byte header.
 */
bool sonar_wav_write_header(uint8_t out[SONAR_WAV_HEADER_BYTES], uint32_t pcm_frames);

uint32_t sonar_wav_data_bytes(uint32_t pcm_frames);
uint32_t sonar_wav_total_bytes(uint32_t pcm_frames);

#endif
