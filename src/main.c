#include "sonar_config.h"
#include "sonar_platform.h"
#include "sonar_rtos.h"
#include "sonar_selftest.h"
#include "sonar_mic_selftest.h"
#include "sonar_mic_config.h"
#include "sonar_mic_task.h"
#include "sonar_console.h"
#include "sonar_component_selftest.h"
#include "sonar_scan_task.h"
#include "sonar_scan_selftest.h"
#include "sonar_motor_config.h"
#include "sonar_speaker_board.h"
#include "sonar_motor_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"

static void report_test(const char *name, bool passed)
{
    xil_printf("TEST %s: %s\r\n", passed ? "PASS" : "FAIL", name);
}

int main(void)
{
    sonar_config_t config = sonar_config_default();
    sonar_timing_t timing;
    sonar_test_result_t tests;
    sonar_test_result_t microphone_tests;
    sonar_test_result_t motor_tests;
    sonar_test_result_t speaker_tests;
    sonar_test_result_t driver_tests;
    sonar_test_result_t codec_tests;
    sonar_profile_t profile;
    uint32_t mismatch;

    xil_printf("\r\nAirborne Circular Sonar - Stage 4\r\n");
    xil_printf("Build: capture-codec fix 2026-09-22\r\n");
    xil_printf("Design reference: %s\r\n", SONAR_MIC_HARDWARE_NAME);
    tests = sonar_selftest_run(report_test);
    microphone_tests = sonar_mic_selftest_run(report_test);
    tests.passed += microphone_tests.passed;
    tests.failed += microphone_tests.failed;
    motor_tests = sonar_motor_selftest_run(report_test);
    speaker_tests = sonar_speaker_selftest_run(report_test);
    tests.passed += motor_tests.passed + speaker_tests.passed;
    tests.failed += motor_tests.failed + speaker_tests.failed;
    driver_tests = sonar_driver_selftest_run(report_test);
    tests.passed += driver_tests.passed;
    tests.failed += driver_tests.failed;
    codec_tests = sonar_codec_selftest_run(report_test);
    tests.passed += codec_tests.passed;
    tests.failed += codec_tests.failed;
    sonar_test_result_t scan_tests = sonar_scan_selftest_run(report_test);
    tests.passed += scan_tests.passed; tests.failed += scan_tests.failed;
    xil_printf("SELFTEST passed=%u failed=%u\r\n", (unsigned int)tests.passed,
               (unsigned int)tests.failed);
    if (tests.failed != 0U) { sonar_halt("software self-test"); }
    if (!sonar_config_validate(&config, (uint32_t)configTICK_RATE_HZ, &timing)) {
        sonar_halt("invalid timing configuration");
    }

    profile = sonar_platform_profile();
    mismatch = sonar_profile_check(&profile);
    xil_printf("BSP GPIO=0x%08x width=%u DMA=0x%08x S2MM=%u IIC=0x%08x\r\n",
        (unsigned int)profile.gpio_base, (unsigned int)profile.gpio_width,
        (unsigned int)profile.dma_base, (unsigned int)profile.dma_has_s2mm,
        (unsigned int)profile.iic_base);
    if (mismatch != 0U) {
        xil_printf("BSP MISMATCH mask=0x%x (GPIO=1 DMA=2 IIC=4); integration blocked.\r\n",
                   (unsigned int)mismatch);
    } else {
        xil_printf("BSP declarations match active BD subset; this does not identify the bitstream.\r\n");
    }
    xil_printf("RTOS tick_hz=%u heartbeat_ticks=%u timeout_ticks=%u\r\n",
        (unsigned int)configTICK_RATE_HZ, (unsigned int)timing.heartbeat,
        (unsigned int)timing.timeout);
#if configSUPPORT_STATIC_ALLOCATION == 1
    xil_printf("RTOS application objects: static allocation\r\n");
#else
    xil_printf("RTOS application objects: startup allocation only\r\n");
#endif
    if (!sonar_console_create()) { sonar_halt("console creation"); }
    if (!sonar_console_input_create()) { sonar_halt("console input creation"); }
    if (!sonar_rtos_create(&timing)) { sonar_halt("RTOS object creation"); }
#if SONAR_MIC_ENABLE_HARDWARE
    /* This export uses two GPIO bits and a processor-controlled codec. */
    if (mismatch == 0U) {
        if (!sonar_mic_task_create()) { sonar_halt("microphone task creation"); }
    } else { xil_printf("MIC disabled: BSP mismatch\r\n"); }
#else
    xil_printf("MIC hardware disabled by configuration\r\n");
#endif
    /* Motor (Tic over PS I2C1) is independent of the PL design. */
    if (!sonar_motor_task_create()) { sonar_halt("motor task creation"); }
    /* Scan task is simulation-only and read the UART directly; enable it
     * again only after it is moved onto sonar_console_input_take(). */
    /* if (!sonar_scan_task_create()) { sonar_halt("scan task creation"); } */
    /* The AMD BSP owns startup, GIC, caches, and the tick source. */
    vTaskStartScheduler();
    sonar_halt("scheduler returned");
    return 1;
}
