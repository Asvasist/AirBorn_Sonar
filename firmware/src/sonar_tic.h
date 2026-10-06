#ifndef SONAR_TIC_H
#define SONAR_TIC_H
#include <stdbool.h>
#include <stdint.h>

/* Bus access to a Pololu Tic stepper controller (I2C). */
typedef struct {
    void *context;
    bool (*write)(void *context, uint8_t address, const uint8_t *data, uint32_t length);
    bool (*read)(void *context, uint8_t address, uint8_t *data, uint32_t length);
    uint32_t (*now)(void *context);
} sonar_tic_io_t;
typedef struct {
    uint8_t address;
    uint32_t max_speed, acceleration, deceleration;
    uint32_t run_ticks, operation_timeout_ticks, keepalive_ticks, watchdog_ticks;
} sonar_tic_config_t;
typedef struct {
    uint8_t operation_state, flags;
    uint16_t errors;
    int32_t position, velocity; /* Controller-commanded microsteps, not encoder feedback. */
} sonar_tic_status_t;
#endif
