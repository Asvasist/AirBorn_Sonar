#ifndef SONAR_SCAN_CONFIG_H
#define SONAR_SCAN_CONFIG_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { SCAN_STEP_AND_STOP, SCAN_CONTINUOUS } sonar_scan_mode_t;
typedef struct {
    sonar_scan_mode_t mode;
    uint32_t shots, capture_bytes, steps_per_revolution;
    int32_t velocity, start_angle_mdeg, step_angle_mdeg, reference_steps;
    bool reference_confirmed;
    uint32_t interval_ms, settle_ms, motion_timeout_ms, capture_timeout_ms;
    uint32_t stop_timeout_ms, frame_hold_timeout_ms;
} sonar_scan_config_t;
typedef struct {
    uint32_t interval, settle, motion_timeout, capture_timeout, stop_timeout, frame_hold_timeout;
} sonar_scan_timing_t;
typedef enum { SCAN_CONFIG_OK, SCAN_CONFIG_ARGUMENT, SCAN_CONFIG_LIMIT,
    SCAN_CONFIG_CALIBRATION, SCAN_CONFIG_TARGET_RANGE, SCAN_CONFIG_TIMING } sonar_scan_config_error_t;

/* Planning defaults only; step scans require explicit calibration/reference. */
sonar_scan_config_t sonar_scan_config_default(void);
sonar_scan_config_error_t sonar_scan_config_validate(const sonar_scan_config_t *config,
    uint32_t tick_hz, sonar_scan_timing_t *timing);
bool sonar_scan_target(const sonar_scan_config_t *config, uint32_t index, int32_t *steps, int32_t *angle_mdeg);
const char *sonar_scan_config_error_name(sonar_scan_config_error_t error);
#endif
