#ifndef SONAR_MOTOR_H
#define SONAR_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { MOTOR_IDLE, MOTOR_RUNNING, MOTOR_DONE, MOTOR_FAULT } sonar_motor_state_t;
typedef enum { MOTOR_OK, MOTOR_START_FAILED, MOTOR_DRIVER_ERROR,
    MOTOR_TIMEOUT, MOTOR_CANCELLED } sonar_motor_fault_t;
#define SONAR_MOTOR_DONE UINT32_C(1)
#define SONAR_MOTOR_ERROR UINT32_C(2)

/* The board driver owns a validated motion plan and its physical units.
 * start() and stop() must be bounded. stop() confirms controller stop status;
 * this is not physical standstill feedback unless the driver has a sensor.
 * Returning false from start() may mean a partially issued command.
 * No motor register map, step count, direction or speed is assumed here. */
typedef struct {
    void *context;
    bool (*start)(void *context, uint32_t generation);
    bool (*stop)(void *context);
} sonar_motor_io_t;

typedef struct {
    sonar_motor_io_t io;
    sonar_motor_state_t state;
    sonar_motor_fault_t fault;
    uint32_t timeout_ticks, start_tick, generation;
    bool stop_confirmed;
} sonar_motor_t;

/* One task owns each service. Initialize once, only while hardware is stopped.
 * Poll at least once per timeout; time differences must remain below 2^31 ticks. */
bool sonar_motor_init(sonar_motor_t *motor, const sonar_motor_io_t *io, uint32_t timeout_ticks);
bool sonar_motor_start(sonar_motor_t *motor, uint32_t now);
/* DONE must come from observed controller completion, never a delay estimate.
 * A matching error takes precedence over DONE; stale generations are ignored. */
void sonar_motor_event(sonar_motor_t *motor, uint32_t generation, uint32_t flags, uint32_t now);
void sonar_motor_poll(sonar_motor_t *motor, uint32_t now);
void sonar_motor_cancel(sonar_motor_t *motor);
bool sonar_motor_release(sonar_motor_t *motor);

#endif
