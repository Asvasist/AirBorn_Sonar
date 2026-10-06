#include "sonar_cycle.h"
#include <stddef.h>

static void fail(sonar_cycle_t *c, const char *reason)
{
    c->fault_state = c->state;
    c->fault_reason = reason;
    c->state = CYCLE_FAULT;
    (void)c->io.stop(c->io.context);
}
bool sonar_cycle_init(sonar_cycle_t *c, const sonar_cycle_config_t *cfg, const sonar_cycle_io_t *io)
{
    if (c == NULL || cfg == NULL || io == NULL || io->capture == NULL || io->move == NULL ||
        io->stop == NULL || cfg->capture_timeout_us == 0U || cfg->motion_timeout_us == 0U ||
        cfg->tx_timeout_us == 0U || cfg->motor_start_us < cfg->tx_timeout_us) {
        return false;
    }
    *c = (sonar_cycle_t){.config = *cfg, .io = *io, .state = CYCLE_IDLE};
    return true;
}
bool sonar_cycle_start(sonar_cycle_t *c, int32_t steps, uint64_t now)
{
    if (c == NULL || steps == 0 || (c->state != CYCLE_IDLE && c->state != CYCLE_STOPPED)) {
        return false;
    }
    c->steps = steps;
    c->stopping = false;
    c->received = false;
    c->transmitted = false;
    c->state = CYCLE_WAIT_BUFFER;
    c->since = now;
    c->last_poll = now;
    return true;
}
bool sonar_cycle_stop(sonar_cycle_t *c, uint64_t now)
{
    if (c == NULL) {
        return false;
    }
    if (!c->io.stop(c->io.context)) {
        if (c->state != CYCLE_FAULT) {
            c->fault_state = c->state;
            c->fault_reason = "STOP_OUTPUTS";
        }
        c->state = CYCLE_FAULT;
        return false;
    }
    c->stopping = true;
    /* The receiver cannot abort an in-flight capture. Drain it without
     * issuing a movement; io.stop has already acknowledged TX abort. */
    if (c->state != CYCLE_ACQUIRE && c->state != CYCLE_FAULT) {
        c->state = CYCLE_STOPPED;
        c->since = now;
    }
    return true;
}
void sonar_cycle_poll(sonar_cycle_t *c, uint64_t now, bool captured, bool transmitted,
                      bool motor_done, bool fault)
{
    if (c == NULL || c->state == CYCLE_FAULT) {
        return;
    }
    if (now < c->last_poll) {
        fail(c, "CLOCK_BACKWARDS");
        return;
    }
    c->last_poll = now;
    if (fault) {
        fail(c, "PERIPHERAL_OR_HEALTH");
        return;
    }
    switch (c->state) {
    case CYCLE_WAIT_BUFFER: {
        uint64_t trigger = now;
        if (c->cycle == UINT32_MAX) {
            fail(c, "SEQUENCE_EXHAUSTED");
            break;
        }
        int result = c->io.capture(c->io.context, c->cycle + 1U, &trigger);
        if (result < 0) {
            fail(c, "CAPTURE_START");
        } else if (result > 0) {
            ++c->cycle;
            c->trigger = trigger;
            c->since = trigger;
            c->received = false;
            c->transmitted = false;
            c->state = CYCLE_ACQUIRE;
        }
        break;
    }
    case CYCLE_ACQUIRE:
        if (captured) {
            c->received = true;
        }
        if (transmitted) {
            c->transmitted = true;
        }
        if (!c->received && now - c->trigger >= c->config.capture_timeout_us) {
            fail(c, "CAPTURE_TIMEOUT");
            break;
        }
        if (!c->transmitted && now - c->trigger >= c->config.tx_timeout_us) {
            fail(c, "TX_TIMEOUT");
            break;
        }
        if (c->received && c->transmitted) {
            c->state = c->stopping ? CYCLE_STOPPED : CYCLE_PRE_MOVE;
            c->since = now;
        }
        break;
    case CYCLE_PRE_MOVE:
        /* Absolute offset from trigger, not an extra delay after DMA completion. */
        if (now - c->trigger >= c->config.motor_start_us) {
            if (!c->io.move(c->io.context, c->steps)) {
                fail(c, "MOTOR_START");
            } else {
                c->state = CYCLE_MOVING;
                c->since = now;
            }
        }
        break;
    case CYCLE_MOVING:
        if (now - c->since >= c->config.motion_timeout_us) {
            fail(c, "MOTOR_TIMEOUT");
        } else if (motor_done) {
            c->state = CYCLE_SETTLING;
            c->since = now;
        }
        break;
    case CYCLE_SETTLING:
        if (now - c->since >= c->config.settle_us) {
            c->state = CYCLE_WAIT_BUFFER;
            c->since = now;
        }
        break;
    default:
        break;
    }
}
const char *sonar_cycle_name(sonar_cycle_state_t state)
{
    static const char *const names[] = {"IDLE",   "WAIT_BUFFER", "ACQUIRE", "PRE_MOVE",
                                        "MOVING", "SETTLING",    "STOPPED", "FAULT"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "INVALID";
}
