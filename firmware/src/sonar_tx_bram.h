#ifndef SONAR_TX_BRAM_H
#define SONAR_TX_BRAM_H
#include <stdbool.h>
#include <stdint.h>
bool sonar_tx_bram_init(void);
uint32_t sonar_tx_bram_capacity(void);
/* Store two PCM16 samples (first in bits 15:0) at sample index 2 * pair. */
bool sonar_tx_bram_write(uint32_t pair, uint32_t packed_pcm);
bool sonar_tx_bram_set_gain(uint32_t samples, uint32_t amplitude_pct);
#endif
