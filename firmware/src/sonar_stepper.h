#ifndef SONAR_STEPPER_H
#define SONAR_STEPPER_H
#include "sonar_tic.h"
typedef enum { STEPPER_IDLE, STEPPER_MOVING, STEPPER_DONE, STEPPER_FAULT } sonar_stepper_state_t;
typedef struct {
    sonar_tic_io_t io;
    sonar_tic_config_t config;
    sonar_stepper_state_t state;
    sonar_tic_status_t status;
    sonar_tic_status_t fault_status;
    const char *fault_reason;
    bool status_valid, fault_status_valid;
    uint32_t divisor, last_keepalive, start_tick;
    int32_t target;
    bool energized;
} sonar_stepper_t;
bool sonar_stepper_init(sonar_stepper_t *, const sonar_tic_config_t *, const sonar_tic_io_t *);
bool sonar_stepper_mode(sonar_stepper_t *, uint32_t divisor);
bool sonar_stepper_move(sonar_stepper_t *, int32_t steps);
bool sonar_stepper_stop(sonar_stepper_t *);
void sonar_stepper_poll(sonar_stepper_t *);
#endif
