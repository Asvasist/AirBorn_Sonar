#ifndef SONAR_TX_BRAM_H
#define SONAR_TX_BRAM_H
#include <stdbool.h>
#include <stdint.h>
bool sonar_tx_bram_init(void);
uint32_t sonar_tx_bram_capacity(void);
bool sonar_tx_bram_write(unsigned bank, uint32_t word, uint32_t packed_pcm);
/* Stride=4 keeps the immutable source in the upper half of every BRAM word. */
bool sonar_tx_bram_set_gain(uint32_t samples, uint32_t amplitude_pct);
void sonar_tx_bram_fence(void);
uint32_t sonar_tx_bram_offset(unsigned bank);
#endif
