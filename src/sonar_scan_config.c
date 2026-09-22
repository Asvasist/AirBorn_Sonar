#include "sonar_scan_config.h"
#include "sonar_config.h"
#include <stddef.h>

sonar_scan_config_t sonar_scan_config_default(void)
{
    return (sonar_scan_config_t){.mode = SCAN_STEP_AND_STOP, .shots = 3U,
        .capture_bytes = 15000U, .velocity = 30000000, .step_angle_mdeg = 10000,
        .interval_ms = 3000U, .settle_ms = 200U, .motion_timeout_ms = 14000U,
        .capture_timeout_ms = 3000U, .stop_timeout_ms = 3000U, .frame_hold_timeout_ms = 5000U};
}
bool sonar_scan_target(const sonar_scan_config_t *config, uint32_t index, int32_t *steps, int32_t *angle_mdeg)
{
    if (config == NULL || steps == NULL || angle_mdeg == NULL || index >= config->shots ||
        config->steps_per_revolution == 0U) { return false; }
    int64_t angle = (int64_t)config->start_angle_mdeg + (int64_t)index * config->step_angle_mdeg;
    if (angle < INT32_MIN || angle > INT32_MAX) { return false; }
    int64_t numerator = angle * config->steps_per_revolution;
    /* Round to the nearest microstep, with halfway values away from zero.
     * Add the rounding offset after division to avoid an int64 overflow. */
    int64_t displacement = numerator / 360000;
    int64_t remainder = numerator % 360000;
    if (remainder >= 180000) { ++displacement; }
    else if (remainder <= -180000) { --displacement; }
    int64_t target = displacement + config->reference_steps;
    if (target < INT32_MIN || target > INT32_MAX) { return false; }
    *steps = (int32_t)target; *angle_mdeg = (int32_t)angle;
    return true;
}
sonar_scan_config_error_t sonar_scan_config_validate(const sonar_scan_config_t *config,
    uint32_t tick_hz, sonar_scan_timing_t *timing)
{
    sonar_scan_timing_t result = {0U};
    int32_t steps, angle;
    if (config == NULL || timing == NULL) { return SCAN_CONFIG_ARGUMENT; }
    if ((config->mode != SCAN_STEP_AND_STOP && config->mode != SCAN_CONTINUOUS) ||
        config->shots == 0U || config->shots > 10000U || config->capture_bytes == 0U ||
        config->capture_bytes > UINT32_C(0x03fffffc) || config->capture_bytes % 4U != 0U ||
        config->velocity == 0 || config->velocity < -500000000 || config->velocity > 500000000 ||
        (config->mode == SCAN_STEP_AND_STOP && config->velocity < 0)) { return SCAN_CONFIG_LIMIT; }
    if (config->mode == SCAN_STEP_AND_STOP) {
        if (!config->reference_confirmed || config->steps_per_revolution == 0U) { return SCAN_CONFIG_CALIBRATION; }
        if (!sonar_scan_target(config, 0U, &steps, &angle) ||
            !sonar_scan_target(config, config->shots - 1U, &steps, &angle)) { return SCAN_CONFIG_TARGET_RANGE; }
    }
    if (!sonar_ms_to_ticks(config->interval_ms, tick_hz, &result.interval) ||
        (config->settle_ms != 0U && !sonar_ms_to_ticks(config->settle_ms, tick_hz, &result.settle)) ||
        !sonar_ms_to_ticks(config->motion_timeout_ms, tick_hz, &result.motion_timeout) ||
        !sonar_ms_to_ticks(config->capture_timeout_ms, tick_hz, &result.capture_timeout) ||
        !sonar_ms_to_ticks(config->stop_timeout_ms, tick_hz, &result.stop_timeout) ||
        !sonar_ms_to_ticks(config->frame_hold_timeout_ms, tick_hz, &result.frame_hold_timeout)) { return SCAN_CONFIG_TIMING; }
    *timing = result;
    return SCAN_CONFIG_OK;
}
const char *sonar_scan_config_error_name(sonar_scan_config_error_t error)
{
    switch (error) {
    case SCAN_CONFIG_OK: return "valid";
    case SCAN_CONFIG_ARGUMENT: return "invalid argument";
    case SCAN_CONFIG_LIMIT: return "mode/count/length/speed outside limits";
    case SCAN_CONFIG_CALIBRATION: return "set steps_per_rev and confirm reference";
    case SCAN_CONFIG_TARGET_RANGE: return "requested angle or motor target overflows";
    case SCAN_CONFIG_TIMING: return "invalid time interval or tick rate";
    default: return "unknown configuration error";
    }
}
