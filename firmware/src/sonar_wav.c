#include "sonar_wav.h"

#include <string.h>

static void put_u16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xffU);
    p[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void put_u32le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffU);
    p[1] = (uint8_t)((value >> 8U) & 0xffU);
    p[2] = (uint8_t)((value >> 16U) & 0xffU);
    p[3] = (uint8_t)((value >> 24U) & 0xffU);
}

uint32_t sonar_wav_data_bytes(uint32_t pcm_frames)
{
    return pcm_frames * SONAR_ACQ_CHANNELS * (SONAR_PCM_BITS_PER_SAMPLE / 8U);
}

uint32_t sonar_wav_total_bytes(uint32_t pcm_frames)
{
    return SONAR_WAV_HEADER_BYTES + sonar_wav_data_bytes(pcm_frames);
}

bool sonar_wav_write_header(uint8_t out[SONAR_WAV_HEADER_BYTES], uint32_t pcm_frames)
{
    if (out == NULL) {
        return false;
    }

    const uint32_t data_bytes = sonar_wav_data_bytes(pcm_frames);
    const uint32_t block_align = SONAR_ACQ_CHANNELS * (SONAR_PCM_BITS_PER_SAMPLE / 8U);
    const uint32_t byte_rate = SONAR_PCM_RATE_HZ * block_align;

    memset(out, 0, SONAR_WAV_HEADER_BYTES);

    memcpy(out + 0, "RIFF", 4);
    put_u32le(out + 4, 36U + data_bytes);
    memcpy(out + 8, "WAVE", 4);

    memcpy(out + 12, "fmt ", 4);
    put_u32le(out + 16, 16U); /* PCM fmt chunk length */
    put_u16le(out + 20, 1U);  /* format = integer PCM */
    put_u16le(out + 22, (uint16_t)SONAR_ACQ_CHANNELS);
    put_u32le(out + 24, SONAR_PCM_RATE_HZ);
    put_u32le(out + 28, byte_rate);
    put_u16le(out + 32, (uint16_t)block_align);
    put_u16le(out + 34, SONAR_PCM_BITS_PER_SAMPLE);

    memcpy(out + 36, "data", 4);
    put_u32le(out + 40, data_bytes);

    return true;
}
