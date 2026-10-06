#ifndef SONAR_PDM_H
#define SONAR_PDM_H

#include "sonar_acquisition_config.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Current Vivado packing (pdm16_to_axis_packer.v):
 *   one 32-bit DMA word = two consecutive 16-channel PDM instants
 *   bits [15:0]  = first instant
 *   bits [31:16] = second instant
 *
 * Within each 16-bit instant:
 *   bit  0..7  = JP1 DATA1..DATA8
 *   bit  8..15 = JP2 DATA11..DATA18
 *
 * The conversion below is intentionally matched to the current
 * 2.4 MHz PDM clock. Decimation is 25 for 96 kHz PCM.
 * The existing 12 kHz FIR cutoff is retained for the current sonar band.
 */
#define SONAR_PCM_RATE_HZ         96000U
#define SONAR_PCM_BITS_PER_SAMPLE 16U
#define SONAR_PDM_DECIMATION      (SONAR_ACQ_PDM_HZ / SONAR_PCM_RATE_HZ)
#define SONAR_PDM_SAMPLES         (SONAR_ACQ_WORDS * 2U)
#define SONAR_PCM_FRAMES          (SONAR_PDM_SAMPLES / SONAR_PDM_DECIMATION)
#define SONAR_PCM_SAMPLES         (SONAR_PCM_FRAMES * SONAR_ACQ_CHANNELS)
#define SONAR_PCM_BYTES           (SONAR_PCM_SAMPLES * sizeof(int16_t))

_Static_assert(SONAR_ACQ_CHANNELS == 16U,
               "sonar_pdm.c is matched to the current 16-channel Vivado packer");
_Static_assert(SONAR_ACQ_PDM_HZ == 2400000U,
               "Re-design the PDM decimator coefficients if the PDM clock changes");
_Static_assert((SONAR_ACQ_PDM_HZ % SONAR_PCM_RATE_HZ) == 0U,
               "PDM clock must divide exactly to the selected PCM rate");
_Static_assert((SONAR_PDM_SAMPLES % SONAR_PDM_DECIMATION) == 0U,
               "Capture length must contain an integer number of PCM frames");

/*
 * Convert one completed DMA PDM frame to signed 16-bit, 16-channel PCM.
 *
 * Output order is WAV-compatible interleaved PCM:
 *   frame0: ch0, ch1, ... ch15
 *   frame1: ch0, ch1, ... ch15
 *   ...
 *
 * The FIR is symmetric and evaluated around each output time, so its
 * group delay is compensated in software. Samples outside the captured
 * interval are treated as zero only at the two frame edges.
 * Not reentrant: uses module-static work buffers. Call from one task only.
 */
bool sonar_pdm_to_pcm16_interleaved(const uint8_t *pdm, uint32_t pdm_bytes, int16_t *pcm,
                                    uint32_t pcm_sample_capacity, uint32_t *pcm_frames_out);

/* Direct-form evaluation of one output sample; the reference for the fast path. */
int16_t sonar_pdm_reference_sample(const uint32_t *words, uint32_t pdm_samples, uint32_t frame,
                                   unsigned channel);

#endif
