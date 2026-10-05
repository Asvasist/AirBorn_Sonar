#ifndef SONAR_WAVEFORM_H
#define SONAR_WAVEFORM_H
#include "sonar_experiment_config.h"
#include <stdbool.h>
#include <stdint.h>
/* Experiment task only. Stream a RIFF/WAVE file through a small parser and
 * write converted mono PCM16 directly to one FPGA BRAM playback buffer.
 * Beginning a replacement invalidates the previous waveform before overwrite.
 * No complete waveform or WAV file is buffered in processor DDR.
 * Supported: 96 kHz PCM8/16/24/32 and IEEE float32, mono or stereo. */
bool sonar_waveform_begin(uint32_t file_bytes, uint32_t file_crc);
bool sonar_waveform_append(uint32_t offset, const uint8_t *data, uint32_t bytes);
bool sonar_waveform_commit(void);
void sonar_waveform_cancel(void);
void sonar_waveform_lock(bool locked);
bool sonar_waveform_uploading(void);
uint32_t sonar_waveform_received(void);
uint32_t sonar_waveform_id(void);
uint32_t sonar_waveform_samples(void);
uint32_t sonar_waveform_crc(void); /* CRC of converted PCM16, excludes pad. */
bool sonar_waveform_offset(uint32_t id, uint32_t *bram_offset);
#endif
