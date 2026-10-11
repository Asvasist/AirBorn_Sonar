#ifndef SONAR_PDM_DECIMATOR_LEGACY_BENCHMARK_H
#define SONAR_PDM_DECIMATOR_LEGACY_BENCHMARK_H
#include "sonar_pdm_decimator.h"
bool sonar_pdm_legacy_to_pcm16_interleaved(const uint8_t *, uint32_t, int16_t *, uint32_t,
                                           uint32_t *);
uint32_t sonar_pdm_legacy_workspace_bytes(void);
bool sonar_pdm_legacy_is_initialized(void);
#endif
