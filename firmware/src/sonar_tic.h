#ifndef SONAR_TIC_H
#define SONAR_TIC_H
#include "sonar_motor.h"
#include <stdbool.h>
#include <stdint.h>

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
typedef struct {
    sonar_tic_io_t io;
    sonar_tic_config_t config;
    sonar_motor_t motor;
    sonar_tic_status_t status;
    int32_t requested_velocity;
    uint32_t motion_tick, last_keepalive;
    bool stopping, status_valid;
} sonar_tic_t;

bool sonar_tic_init(sonar_tic_t *tic, const sonar_tic_config_t *config, const sonar_tic_io_t *io);
bool sonar_tic_start(sonar_tic_t *tic, int32_t velocity);
void sonar_tic_poll(sonar_tic_t *tic);
void sonar_tic_cancel(sonar_tic_t *tic);
bool sonar_tic_status(sonar_tic_t *tic);
#endif
