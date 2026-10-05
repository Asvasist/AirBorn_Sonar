#include "sonar_sequencer.h"
#include "sonar_acquisition_config.h"
#include "sonar_config.h"
#include "sonar_platform.h"
#include "sonar_rtos.h"
#include "sonar_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"

int main(void)
{
    sonar_config_t config = sonar_config_default();
    sonar_timing_t timing;
    sonar_profile_t profile = sonar_platform_profile();
    uint32_t mismatch = sonar_profile_check(&profile);

    xil_printf("\r\nAirborne Circular Sonar\r\n");
    xil_printf("Design reference: %s\r\n", SONAR_HARDWARE_NAME);
    if (!sonar_config_validate(&config, (uint32_t)configTICK_RATE_HZ, &timing)) {
        sonar_halt("invalid timing configuration");
    }
    xil_printf("BSP GPIO=0x%08x width=%u DMA=0x%08x S2MM=%u IIC=0x%08x\r\n",
        (unsigned int)profile.gpio_base, (unsigned int)profile.gpio_width,
        (unsigned int)profile.dma_base, (unsigned int)profile.dma_has_s2mm,
        (unsigned int)profile.iic_base);
    if (mismatch != 0U) {
        xil_printf("BSP MISMATCH mask=0x%x (GPIO=1 DMA=2 IIC=4)\r\n", (unsigned int)mismatch);
        sonar_halt("BSP mismatch");
    }
    xil_printf("RTOS tick_hz=%u heartbeat_ticks=%u timeout_ticks=%u\r\n",
        (unsigned int)configTICK_RATE_HZ, (unsigned int)timing.heartbeat,
        (unsigned int)timing.timeout);
    if (!sonar_console_create()) { sonar_halt("console creation"); }
    if (!sonar_rtos_create(&timing)) { sonar_halt("RTOS object creation"); }
    if (!sonar_sequencer_create()) { sonar_halt("sequencer task creation"); }
    /* The AMD BSP owns startup, GIC, caches, and the tick source. */
    vTaskStartScheduler();
    sonar_halt("scheduler returned");
    return 1;
}
