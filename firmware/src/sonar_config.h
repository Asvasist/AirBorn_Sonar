#ifndef SONAR_CONFIG_H
#define SONAR_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* These are software health-check periods, never acoustic timing parameters. */
#define SONAR_HEARTBEAT_MS 100U
#define SONAR_TIMEOUT_MS   500U
#define SONAR_REPORT_MS    1000U
#define SONAR_TASK_STACK_WORDS 1024U
#define SONAR_QUEUE_LENGTH 4U

typedef struct {
    uint32_t heartbeat_ms;
    uint32_t timeout_ms;
    uint32_t report_ms;
} sonar_config_t;

typedef struct {
    uint32_t heartbeat;
    uint32_t timeout;
    uint32_t report;
} sonar_timing_t;

sonar_config_t sonar_config_default(void);
/* Round up to whole ticks; reject zero and intervals outside the wrap-safe range. */
bool sonar_ms_to_ticks(uint32_t ms, uint32_t tick_hz, uint32_t *ticks);
/* Leave *timing unchanged on failure. Timeout must exceed two heartbeat periods. */
bool sonar_config_validate(const sonar_config_t *config, uint32_t tick_hz,
                           sonar_timing_t *timing);

#endif
