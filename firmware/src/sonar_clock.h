#ifndef SONAR_CLOCK_H
#define SONAR_CLOCK_H
#include <stdint.h>
#include "xiltimer.h"

/* Processor clock, in microseconds. This is not an FPGA sample timestamp. */
static inline uint64_t sonar_clock_us(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    /* AMD defines COUNTS_PER_SECOND as CPU_HZ/2 without parentheses.
     * Evaluate it first: direct division/modulo by that macro changes the
     * arithmetic precedence and can make reported time move backwards. */
    const uint64_t frequency = (uint64_t)(COUNTS_PER_SECOND);
    return (ticks / frequency) * 1000000U + (ticks % frequency) * 1000000U / frequency;
}
#endif
