#ifndef SONAR_PL_GPIO_H
#define SONAR_PL_GPIO_H
#include <stdbool.h>
#include <stdint.h>
#define SONAR_PL_TRIGGER UINT32_C(1)
#define SONAR_PL_CODEC_READY UINT32_C(2)
/* Shared two-bit GPIO owner. Task callers only; updates are indivisible.
 * First init drives both bits low. Later init calls preserve the output.
 * A readback failure latches until processor reset. */
bool sonar_pl_gpio_init(void);
bool sonar_pl_gpio_update(uint32_t mask, uint32_t value);
#endif
