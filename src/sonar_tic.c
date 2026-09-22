#include "sonar_tic.h"
#include <stddef.h>

static uint32_t now(sonar_tic_t *tic) { return tic->io.now(tic->io.context); }
static bool quick(sonar_tic_t *tic, uint8_t command)
{ return tic->io.write(tic->io.context, tic->config.address, &command, 1U); }
static bool command32(sonar_tic_t *tic, uint8_t command, uint32_t value)
{
    uint8_t bytes[5] = {command, 0U, 0U, 0U, 0U};
    for (unsigned i = 0U; i < 4U; ++i) { bytes[i + 1U] = (uint8_t)(value >> (8U * i)); }
    return tic->io.write(tic->io.context, tic->config.address, bytes, 5U);
}
static bool variables(sonar_tic_t *tic, uint8_t offset, uint8_t *data, uint32_t length)
{
    uint8_t command[2] = {0xa1U, offset};
    /* Pololu permits STOP then START for the block-read response. */
    return tic->io.write(tic->io.context, tic->config.address, command, 2U) &&
        tic->io.read(tic->io.context, tic->config.address, data, length);
}
static int32_t signed32(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    /* Avoid implementation-defined conversion of out-of-range unsigned values. */
    if (value <= INT32_MAX) { return (int32_t)value; }
    return -1 - (int32_t)(UINT32_MAX - value);
}
bool sonar_tic_status(sonar_tic_t *tic)
{
    uint8_t general[4], motion[8];
    if (tic == NULL) { return false; }
    tic->status_valid = false;
    if (!variables(tic, 0U, general, 4U) || !variables(tic, 0x22U, motion, 8U)) { return false; }
    tic->status = (sonar_tic_status_t){general[0], general[1],
        (uint16_t)((uint16_t)general[2] | ((uint16_t)general[3] << 8U)),
        signed32(motion), signed32(motion + 4)};
    tic->status_valid = true;
    if (tic->motor.state == MOTOR_IDLE) {
        tic->motor.stop_confirmed = (tic->status.flags & 1U) == 0U && tic->status.velocity == 0;
    }
    return true;
}
static bool stop_motor(void *context)
{
    sonar_tic_t *tic = context;
    bool halted = quick(tic, 0x89U);
    bool disabled = quick(tic, 0x86U); /* Attempt even if halt failed. */
    bool readback = sonar_tic_status(tic);
    return halted && disabled && readback && (tic->status.flags & 1U) == 0U && tic->status.velocity == 0;
}
static bool start_motor(void *context, uint32_t generation)
{
    sonar_tic_t *tic = context;
    (void)generation;
    /* Set a zero target before enabling. Keep the user's stored current limit
     * and microstep mode; these depend on the actual motor and mechanics. */
    if (!command32(tic, 0xe3U, 0U) || !command32(tic, 0xe6U, tic->config.max_speed) ||
        !command32(tic, 0xeaU, tic->config.acceleration) || !command32(tic, 0xe9U, tic->config.deceleration) ||
        !quick(tic, 0x8cU) || !quick(tic, 0x85U) || !quick(tic, 0x83U) ||
        !command32(tic, 0xe3U, (uint32_t)tic->requested_velocity)) { return false; }
    tic->last_keepalive = now(tic);
    tic->motion_tick = tic->last_keepalive;
    tic->stopping = false;
    return true;
}
bool sonar_tic_init(sonar_tic_t *tic, const sonar_tic_config_t *config, const sonar_tic_io_t *io)
{
    if (tic == NULL || config == NULL || io == NULL || io->write == NULL || io->read == NULL || io->now == NULL ||
        config->address < 8U || config->address > 119U || config->max_speed == 0U || config->max_speed > 500000000U ||
        config->acceleration < 100U || config->acceleration > INT32_MAX || config->deceleration < 100U ||
        config->deceleration > INT32_MAX || config->run_ticks == 0U ||
        config->operation_timeout_ticks <= config->run_ticks || config->operation_timeout_ticks >= UINT32_C(0x80000000) ||
        config->keepalive_ticks == 0U || config->watchdog_ticks >= UINT32_C(0x80000000) ||
        (uint64_t)config->keepalive_ticks * 3U >= config->watchdog_ticks) { return false; }
    *tic = (sonar_tic_t){.io = *io, .config = *config};
    sonar_motor_io_t motor_io = {tic, start_motor, stop_motor};
    if (!sonar_motor_init(&tic->motor, &motor_io, config->operation_timeout_ticks)) { return false; }
    tic->motor.stop_confirmed = false;
    return true;
}
bool sonar_tic_start(sonar_tic_t *tic, int32_t velocity)
{
    if (tic == NULL || velocity == 0 || (int64_t)velocity > tic->config.max_speed ||
        -(int64_t)velocity > tic->config.max_speed) { return false; }
    if (tic->motor.state == MOTOR_DONE) { (void)sonar_motor_release(&tic->motor); }
    if (tic->motor.state != MOTOR_IDLE) { return false; }
    tic->requested_velocity = velocity;
    return sonar_motor_start(&tic->motor, now(tic));
}
static void driver_error(sonar_tic_t *tic)
{ sonar_motor_event(&tic->motor, tic->motor.generation, SONAR_MOTOR_ERROR, now(tic)); }
void sonar_tic_poll(sonar_tic_t *tic)
{
    if (tic == NULL || tic->motor.state != MOTOR_RUNNING) { return; }
    uint32_t tick = now(tic);
    sonar_motor_poll(&tic->motor, tick);
    if (tic->motor.state != MOTOR_RUNNING) { return; }
    uint32_t elapsed = tick - tic->last_keepalive;
    /* Do not silently recover a late watchdog by resuming the old target. */
    if (elapsed >= tic->config.watchdog_ticks - tic->config.keepalive_ticks) { driver_error(tic); return; }
    if (elapsed >= tic->config.keepalive_ticks) {
        if (!quick(tic, 0x8cU)) { driver_error(tic); return; }
        tic->last_keepalive = now(tic);
    }
    if (!tic->stopping && (uint32_t)(now(tic) - tic->motion_tick) >= tic->config.run_ticks) {
        tic->stopping = true;
        if (!command32(tic, 0xe3U, 0U)) { driver_error(tic); return; }
    }
    if (!sonar_tic_status(tic) || tic->status.errors != 0U || tic->status.operation_state != 10U ||
        (tic->status.flags & 1U) == 0U) { driver_error(tic); return; }
    if (tic->stopping && tic->status.velocity == 0) {
        if (!quick(tic, 0x86U) || !sonar_tic_status(tic) || (tic->status.flags & 1U) != 0U ||
            tic->status.velocity != 0) { driver_error(tic); return; }
        sonar_motor_event(&tic->motor, tic->motor.generation, SONAR_MOTOR_DONE, now(tic));
    } else { sonar_motor_poll(&tic->motor, now(tic)); }
}
void sonar_tic_cancel(sonar_tic_t *tic)
{
    if (tic == NULL || tic->motor.state == MOTOR_FAULT) { return; }
    if (tic->motor.state == MOTOR_RUNNING) { sonar_motor_cancel(&tic->motor); }
    else {
        /* An explicit halt also covers a pre-existing target after a PS restart. */
        tic->motor.state = MOTOR_FAULT;
        tic->motor.fault = MOTOR_CANCELLED;
        tic->motor.stop_confirmed = stop_motor(tic);
    }
}
