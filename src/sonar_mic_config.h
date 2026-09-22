#ifndef SONAR_MIC_CONFIG_H
#define SONAR_MIC_CONFIG_H

/* Parameters verified in the current AirbornSonar_Initialtest.xsa. */
#define SONAR_MIC_HARDWARE_NAME "AirbornSonar_Initialtest.xsa"
#define SONAR_MIC_CAPTURE_WORDS 3750U
#define SONAR_MIC_PDM_HZ 2400000U
#define SONAR_MIC_CAPTURE_BYTES (SONAR_MIC_CAPTURE_WORDS * 4U)
#define SONAR_MIC_BUFFER_BYTES ((SONAR_MIC_CAPTURE_BYTES + 31U) & ~31U)
#define SONAR_MIC_TIMEOUT_MS 500U
/* 80 x 1200 samples at 48 kHz = 2 s TX; margin only, not observed TX completion. */
#define SONAR_MIC_RETRIGGER_MS 2100U
#define SONAR_MIC_RESET_TIMEOUT_MS 100U
#define SONAR_MIC_POLL_MS 20U
#define SONAR_MIC_STACK_WORDS 1024U

/* Set to 0 for processor-only self-tests without accessing the PL. */
#ifndef SONAR_MIC_ENABLE_HARDWARE
#define SONAR_MIC_ENABLE_HARDWARE 1
#endif

#endif
