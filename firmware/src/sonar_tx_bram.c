#include "sonar_tx_bram.h"
#include "sonar_audio_hw.h"
#include "sonar_experiment_config.h"
#include "xil_io.h"
#include "xil_mmu.h"

/* Each 32-bit BRAM word holds one sample: the uploaded PCM16 in bits 31:16 and
 * the amplitude-scaled copy the FPGA plays in bits 15:0. Keeping the original
 * lets the amplitude change without a new upload. */
#define SAMPLE_STRIDE 4U
#define CAPACITY ((SONAR_TX_BRAM_BYTES / SAMPLE_STRIDE) < SONAR_TX_MAX_SAMPLES ? \
                  (SONAR_TX_BRAM_BYTES / SAMPLE_STRIDE) : SONAR_TX_MAX_SAMPLES)

static bool initialized;

uint32_t sonar_tx_bram_capacity(void)
{
    return SONAR_TX_BRAM_BASEADDR != 0U ? CAPACITY : 0U;
}

bool sonar_tx_bram_init(void)
{
    if (initialized) { return true; }
    if (SONAR_TX_BRAM_BASEADDR == 0U) { return false; }
    /* The BRAM sits in a 1 MiB section that must be device memory, not cached DDR. */
    Xil_SetTlbAttributes((INTPTR)(SONAR_TX_BRAM_BASEADDR & ~UINT32_C(0xfffff)), DEVICE_MEMORY);
    initialized = true;
    return true;
}

bool sonar_tx_bram_write(uint32_t pair, uint32_t packed)
{
    if (!initialized || pair >= (CAPACITY + 1U) / 2U) { return false; }
    UINTPTR at = (UINTPTR)SONAR_TX_BRAM_BASEADDR + pair * 2U * SAMPLE_STRIDE;
    uint32_t first = packed & 0xffffU, second = packed >> 16U;
    Xil_Out32(at, (first << 16U) | first);
    if (pair * 2U + 1U < CAPACITY) { Xil_Out32(at + SAMPLE_STRIDE, (second << 16U) | second); }
    SYNCHRONIZE_IO;
    return true;
}

bool sonar_tx_bram_set_gain(uint32_t samples, uint32_t amplitude_pct)
{
    if (!initialized || samples == 0U || samples > CAPACITY || amplitude_pct > 100U) {
        return false;
    }
    for (uint32_t i = 0U; i < samples; ++i) {
        UINTPTR at = (UINTPTR)SONAR_TX_BRAM_BASEADDR + i * SAMPLE_STRIDE;
        uint32_t original = Xil_In32(at) >> 16U;
        int32_t product = (int32_t)(int16_t)(uint16_t)original * (int32_t)amplitude_pct;
        /* Round to nearest, ties away from zero. 100 % restores the exact source. */
        int32_t scaled = product >= 0 ? (product + 50) / 100 : -((-product + 50) / 100);
        Xil_Out32(at, (original << 16U) | (uint16_t)(int16_t)scaled);
    }
    SYNCHRONIZE_IO;
    return true;
}
