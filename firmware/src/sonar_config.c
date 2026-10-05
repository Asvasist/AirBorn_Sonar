#include "sonar_config.h"
#include <stddef.h>

sonar_config_t sonar_config_default(void)
{
    const sonar_config_t config = {SONAR_HEARTBEAT_MS, SONAR_TIMEOUT_MS, SONAR_REPORT_MS};
    return config;
}

bool sonar_ms_to_ticks(uint32_t ms, uint32_t tick_hz, uint32_t *ticks)
{
    uint64_t rounded;
    if (ms == 0U || tick_hz == 0U || ticks == NULL) { return false; }
    rounded = ((uint64_t)ms * tick_hz + 999U) / 1000U;
    /* Modular elapsed-time comparisons require intervals below half a cycle. */
    if (rounded == 0U || rounded > UINT32_MAX / 2U) { return false; }
    *ticks = (uint32_t)rounded;
    return true;
}

bool sonar_config_validate(const sonar_config_t *config, uint32_t tick_hz,
                           sonar_timing_t *timing)
{
    sonar_timing_t result;
    if (config == NULL || timing == NULL) { return false; }
    if (!sonar_ms_to_ticks(config->heartbeat_ms, tick_hz, &result.heartbeat) ||
        !sonar_ms_to_ticks(config->timeout_ms, tick_hz, &result.timeout) ||
        !sonar_ms_to_ticks(config->report_ms, tick_hz, &result.report)) { return false; }
    if ((uint64_t)result.timeout <= 2U * (uint64_t)result.heartbeat ||
        result.report < result.heartbeat) { return false; }
    *timing = result;
    return true;
}
