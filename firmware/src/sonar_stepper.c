#include "sonar_stepper.h"
#include <stddef.h>

static bool quick(sonar_stepper_t *s, uint8_t command)
{
    return s->io.write(s->io.context, s->config.address, &command, 1U);
}
static bool word(sonar_stepper_t *s, uint8_t command, uint32_t value)
{
    uint8_t data[5] = {command};
    for (unsigned i = 0; i < 4U; ++i) {
        data[i + 1U] = (uint8_t)(value >> (8U * i));
    }
    return s->io.write(s->io.context, s->config.address, data, sizeof(data));
}
static bool read_vars(sonar_stepper_t *s, uint8_t offset, uint8_t *out, uint32_t n)
{
    const uint8_t request[2] = {0xa1U, offset};
    return s->io.write(s->io.context, s->config.address, request, 2U) &&
           s->io.read(s->io.context, s->config.address, out, n);
}
static int32_t signed_word(const uint8_t *p)
{
    uint32_t n = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static bool status(sonar_stepper_t *s)
{
    uint8_t g[4], m[8];
    s->status_valid = false;
    if (!read_vars(s, 0U, g, 4U) || !read_vars(s, 0x22U, m, 8U)) {
        return false;
    }
    s->status = (sonar_tic_status_t){g[0], g[1], (uint16_t)((uint16_t)g[2] | (uint16_t)g[3] << 8),
                                     signed_word(m), signed_word(m + 4)};
    s->status_valid = true;
    return true;
}
bool sonar_stepper_stop(sonar_stepper_t *s)
{
    if (s == NULL || s->io.write == NULL) {
        return false;
    }
    bool halt = quick(s, 0x89U);
    bool disable = quick(s, 0x86U); /* Attempt shutdown even after a failed halt. */
    bool ok =
        status(s) && halt && disable && (s->status.flags & 1U) == 0U && s->status.velocity == 0;
    s->energized = false;
    s->state = ok && s->fault_reason == NULL ? STEPPER_IDLE : STEPPER_FAULT;
    return ok;
}
static void fault(sonar_stepper_t *s, const char *reason)
{
    if (s->fault_reason == NULL) {
        s->fault_reason = reason;
        s->fault_status = s->status;
        s->fault_status_valid = s->status_valid;
    }
    (void)sonar_stepper_stop(s);
    s->state = STEPPER_FAULT;
}
bool sonar_stepper_init(sonar_stepper_t *s, const sonar_tic_config_t *cfg, const sonar_tic_io_t *io)
{
    if (s == NULL || cfg == NULL || io == NULL || io->now == NULL || io->read == NULL ||
        io->write == NULL || cfg->max_speed == 0U || cfg->max_speed > 500000000U ||
        cfg->keepalive_ticks == 0U || (uint64_t)cfg->keepalive_ticks * 3U >= cfg->watchdog_ticks ||
        cfg->operation_timeout_ticks == 0U || cfg->operation_timeout_ticks >= 0x80000000U) {
        return false;
    }
    *s = (sonar_stepper_t){.io = *io, .config = *cfg};
    /* Preserve the stored coil-current limit; motor ratings are not known. */
    if (!sonar_stepper_stop(s) || !word(s, 0xe6U, cfg->max_speed) || !word(s, 0xe5U, 0U) ||
        !word(s, 0xeaU, cfg->acceleration) || !word(s, 0xe9U, cfg->deceleration) ||
        !word(s, 0xecU, 0U) || !sonar_stepper_mode(s, 1U)) {
        fault(s, "CONFIGURE");
        return false;
    }
    return true;
}
bool sonar_stepper_mode(sonar_stepper_t *s, uint32_t divisor)
{
    static const uint16_t divisors[] = {1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U};
    if (s == NULL || s->state != STEPPER_IDLE || s->energized) {
        return false;
    }
    for (unsigned i = 0U; i < sizeof(divisors) / sizeof(divisors[0]); ++i) {
        if (divisor == divisors[i]) {
            /* Tic 36v4 reserves mode 6 for another model's special half-step. */
            uint8_t mode = (uint8_t)(i < 6U ? i : i + 1U);
            const uint8_t request[2] = {0x94U, mode};
            uint8_t actual;
            if (!s->io.write(s->io.context, s->config.address, request, 2U) ||
                !read_vars(s, 0x49U, &actual, 1U) || actual != mode) {
                fault(s, "STEP_MODE");
                return false;
            }
            /* Positions in different resolutions are not interchangeable.
             * Establish a fresh relative origin without moving the shaft. */
            if (!word(s, 0xecU, 0U) || !status(s)) {
                fault(s, "ORIGIN");
                return false;
            }
            s->divisor = divisor;
            return true;
        }
    }
    return false;
}
bool sonar_stepper_move(sonar_stepper_t *s, int32_t steps)
{
    if (s == NULL || steps == 0 || (s->state != STEPPER_IDLE && s->state != STEPPER_DONE)) {
        return false;
    }
    if (!status(s) || s->status.velocity != 0) {
        fault(s, "NOT_STATIONARY_OR_BUS");
        return false;
    }
    int64_t target = (int64_t)s->status.position + steps;
    if (target < INT32_MIN || target > INT32_MAX) {
        fault(s, "POSITION_RANGE");
        return false;
    }
    s->target = (int32_t)target;
    /* Establish a stationary target before enabling; never resume an old move. */
    if (!word(s, 0xecU, (uint32_t)s->status.position) ||
        !word(s, 0xe0U, (uint32_t)s->status.position) || !quick(s, 0x8cU) || !quick(s, 0x85U) ||
        !quick(s, 0x83U) || !word(s, 0xe0U, (uint32_t)s->target)) {
        fault(s, "MOVE_COMMAND");
        return false;
    }
    s->energized = true;
    s->state = STEPPER_MOVING;
    s->last_keepalive = s->io.now(s->io.context);
    s->start_tick = s->last_keepalive;
    return true;
}
void sonar_stepper_poll(sonar_stepper_t *s)
{
    if (s == NULL || s->state == STEPPER_FAULT || !s->energized) {
        return;
    }
    uint32_t now = s->io.now(s->io.context);
    uint32_t elapsed = now - s->last_keepalive;
    if (elapsed >= s->config.watchdog_ticks - s->config.keepalive_ticks) {
        fault(s, "KEEPALIVE_LATE");
        return;
    }
    if (elapsed >= s->config.keepalive_ticks) {
        if (!quick(s, 0x8cU)) {
            fault(s, "KEEPALIVE_WRITE");
            return;
        }
        s->last_keepalive = s->io.now(s->io.context);
    }
    if (s->state == STEPPER_MOVING && now - s->start_tick >= s->config.operation_timeout_ticks) {
        fault(s, "MOVE_TIMEOUT");
        return;
    }
    if (!status(s) || s->status.errors != 0U || s->status.operation_state != 10U ||
        (s->status.flags & 1U) == 0U) {
        fault(s, "CONTROLLER_STATUS_OR_BUS");
        return;
    }
    if (s->state == STEPPER_MOVING) {
        uint8_t plan[5];
        if (!read_vars(s, 9U, plan, 5U) || plan[0] != 1U || signed_word(plan + 1) != s->target) {
            fault(s, "TARGET_READBACK");
            return;
        }
        if (s->status.position == s->target && s->status.velocity == 0) {
            s->state = STEPPER_DONE;
        }
    }
}
