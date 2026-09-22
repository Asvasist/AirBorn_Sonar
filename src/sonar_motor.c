#include "sonar_motor.h"
#include <stddef.h>

static void fail(sonar_motor_t *motor, sonar_motor_fault_t fault)
{
    motor->state = MOTOR_FAULT;
    motor->fault = fault;
    motor->stop_confirmed = motor->io.stop(motor->io.context);
}

bool sonar_motor_init(sonar_motor_t *motor, const sonar_motor_io_t *io, uint32_t timeout_ticks)
{
    if (motor == NULL || io == NULL || io->start == NULL || io->stop == NULL ||
        timeout_ticks == 0U || timeout_ticks >= UINT32_C(0x80000000)) { return false; }
    *motor = (sonar_motor_t){.io = *io, .state = MOTOR_IDLE, .fault = MOTOR_OK,
        .timeout_ticks = timeout_ticks, .stop_confirmed = true};
    return true;
}

bool sonar_motor_start(sonar_motor_t *motor, uint32_t now)
{
    if (motor == NULL || motor->state != MOTOR_IDLE) { return false; }
    ++motor->generation;
    if (motor->generation == 0U) { ++motor->generation; }
    motor->start_tick = now;
    motor->stop_confirmed = false;
    motor->state = MOTOR_RUNNING;
    if (!motor->io.start(motor->io.context, motor->generation)) {
        fail(motor, MOTOR_START_FAILED);
        return false;
    }
    return true;
}

void sonar_motor_poll(sonar_motor_t *motor, uint32_t now)
{
    if (motor != NULL && motor->state == MOTOR_RUNNING &&
        (uint32_t)(now - motor->start_tick) >= motor->timeout_ticks) { fail(motor, MOTOR_TIMEOUT); }
}

void sonar_motor_event(sonar_motor_t *motor, uint32_t generation, uint32_t flags, uint32_t now)
{
    if (motor == NULL || motor->state != MOTOR_RUNNING) { return; }
    sonar_motor_poll(motor, now);
    if (motor->state != MOTOR_RUNNING || generation != motor->generation) { return; }
    if ((flags & SONAR_MOTOR_ERROR) != 0U) { fail(motor, MOTOR_DRIVER_ERROR); }
    else if ((flags & SONAR_MOTOR_DONE) != 0U) {
        motor->state = MOTOR_DONE;
        motor->stop_confirmed = true;
    }
}

void sonar_motor_cancel(sonar_motor_t *motor)
{
    if (motor != NULL && motor->state == MOTOR_RUNNING) { fail(motor, MOTOR_CANCELLED); }
}

bool sonar_motor_release(sonar_motor_t *motor)
{
    if (motor == NULL || motor->state != MOTOR_DONE) { return false; }
    motor->state = MOTOR_IDLE;
    return true;
}
