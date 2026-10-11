#ifndef SONAR_PDM_DECIMATOR_H
#define SONAR_PDM_DECIMATOR_H

#include "sonar_acquisition_config.h"
#include <stdbool.h>
#include <stdint.h>

#define SONAR_PCM_RATE_HZ         96000U
#define SONAR_PCM_BITS_PER_SAMPLE 16U
#define SONAR_PDM_DECIMATION      (SONAR_ACQ_PDM_HZ / SONAR_PCM_RATE_HZ)
#define SONAR_PDM_SAMPLES         (SONAR_ACQ_WORDS * 2U)
#define SONAR_PCM_FRAMES          (SONAR_PDM_SAMPLES / SONAR_PDM_DECIMATION)
#define SONAR_PCM_SAMPLES         (SONAR_PCM_FRAMES * SONAR_ACQ_CHANNELS)
#define SONAR_PCM_BYTES           (SONAR_PCM_SAMPLES * sizeof(int16_t))
#define SONAR_RX_PASSBAND_HZ      46000U
#define SONAR_RX_STOPBAND_HZ      50000U

#ifndef SONAR_PDM_TEST_REFERENCE
#define SONAR_PDM_TEST_REFERENCE 0
#endif

_Static_assert(SONAR_ACQ_CHANNELS == 16U, "Wire packing requires 16 channels");
_Static_assert(SONAR_ACQ_PDM_HZ == 2400000U, "Coefficients require 2.4 MHz PDM");
_Static_assert(SONAR_PDM_DECIMATION == 25U, "Decimation is 5 then 5");
_Static_assert(SONAR_PDM_SAMPLES % SONAR_PDM_DECIMATION == 0U,
               "Capture must contain a whole number of PCM frames");

/* One DMA word packs two consecutive 16-channel instants; bit c is channel c.
 * Convert 2.4 MHz PDM -> 480 kHz Q28 -> 96 kHz PCM16, using centered symmetric
 * FIRs. The original PDM stream is extended with zeros at capture boundaries.
 * Group delay is compensated by centering; no extra delay is added to WAV time.
 * The effective half-window is about 0.613 ms: edge samples include padding.
 * One task owns conversion. Module-static buffers are not reentrant.
 * Input, output, and frame-count buffers must be disjoint. Checked invalid
 * arguments are rejected before changing PCM or frame count.
 */
bool sonar_pdm_to_pcm16_interleaved(const uint8_t *pdm, uint32_t pdm_bytes, int16_t *pcm,
                                    uint32_t pcm_sample_capacity, uint32_t *pcm_frames_out);
uint32_t sonar_pdm_workspace_bytes(void);
bool sonar_pdm_is_initialized(void);

#if SONAR_PDM_TEST_REFERENCE
int16_t sonar_pdm_reference_sample(const uint32_t *words, uint32_t pdm_samples, uint32_t frame,
                                   unsigned channel);
#endif

#endif
