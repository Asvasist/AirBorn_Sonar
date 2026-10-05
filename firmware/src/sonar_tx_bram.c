#include "sonar_tx_bram.h"
#include "sonar_wav_board_config.h"
#include "sonar_experiment_config.h"
#include "sonar_profile.h"
#include "xil_io.h"
#include "xil_mmu.h"
static bool initialized;
uint32_t sonar_tx_bram_capacity(void)
{
    uint64_t base=SONAR_WAV_BRAM_BASE, bytes=SONAR_WAV_BRAM_BYTES;
    if (!SONAR_WAV_ENABLED || base<0x40000000U || (base & 0xfffU) ||
        bytes<4U || (bytes & 3U) || base+bytes>UINT64_C(0xc0000000) ||
        (SONAR_WAV_SAMPLE_STRIDE!=2U && SONAR_WAV_SAMPLE_STRIDE!=4U)) { return 0U; }
    const uint32_t used[]={SONAR_EXPECTED_GPIO_BASE,SONAR_EXPECTED_DMA_BASE,
        SONAR_EXPECTED_IIC_BASE,0x41210000U,0x41220000U};
    for (unsigned i=0;i<sizeof(used)/sizeof(used[0]);++i) {
        if (base<(uint64_t)used[i]+0x10000U && base+bytes>used[i]) { return 0U; }
    }
    uint32_t n=(uint32_t)(bytes/SONAR_WAV_SAMPLE_STRIDE);
    return n<SONAR_TX_MAX_SAMPLES?n:SONAR_TX_MAX_SAMPLES;
}
bool sonar_tx_bram_init(void)
{
    if (initialized) { return true; }
    uint32_t capacity=sonar_tx_bram_capacity();
    if (!capacity) { return false; }
    uint64_t base=SONAR_WAV_BRAM_BASE;
    uint64_t last=base+(uint64_t)capacity*SONAR_WAV_SAMPLE_STRIDE-1U;
    /* Zynq-7000 section mappings have 1 MiB granularity. Map only the sections
     * covering the used playback buffer, even when the AXI aperture is larger. */
    for (uint64_t page=base & ~UINT64_C(0xfffff); page<=last; page+=0x100000U) {
        Xil_SetTlbAttributes((INTPTR)page, DEVICE_MEMORY);
    }
    initialized=true; return true;
}
bool sonar_tx_bram_write(unsigned bank, uint32_t word, uint32_t packed)
{
    uint32_t count=sonar_tx_bram_capacity();
    if (!initialized || bank!=0U || word>=(count+1U)/2U) { return false; }
    UINTPTR at=(UINTPTR)SONAR_WAV_BRAM_BASE+word*2U*SONAR_WAV_SAMPLE_STRIDE;
    if (SONAR_WAV_SAMPLE_STRIDE==2U) { Xil_Out32(at,packed); }
    else {
        uint32_t first=packed&0xffffU, second=packed>>16U;
        Xil_Out32(at,(first<<16U)|first);
        if (word*2U+1U<count) { Xil_Out32(at+4U,(second<<16U)|second); }
    }
    return true;
}
bool sonar_tx_bram_set_gain(uint32_t samples, uint32_t amplitude_pct)
{
    if (!initialized || SONAR_WAV_SAMPLE_STRIDE!=4U || !samples ||
        samples>sonar_tx_bram_capacity() || amplitude_pct>100U) { return false; }
    for (uint32_t i=0;i<samples;++i) {
        UINTPTR at=(UINTPTR)SONAR_WAV_BRAM_BASE+i*4U;
        uint32_t word=Xil_In32(at), original=word>>16U;
        int32_t value=(int16_t)(uint16_t)original;
        int32_t product=value*(int32_t)amplitude_pct;
        /* Round to nearest, ties away from zero. Unity restores exact source. */
        int32_t scaled=product>=0?(product+50)/100:-((-product+50)/100);
        Xil_Out32(at,(original<<16U)|(uint16_t)(int16_t)scaled);
    }
    SYNCHRONIZE_IO;
    return true;
}
void sonar_tx_bram_fence(void) { SYNCHRONIZE_IO; }
uint32_t sonar_tx_bram_offset(unsigned bank) { (void)bank; return 0U; }
