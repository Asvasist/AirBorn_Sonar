#include "sonar_health.h"
#include <stddef.h>

bool sonar_health_init(sonar_health_t *health, uint32_t now, uint32_t timeout)
{
    if (health == NULL) { return false; }
    health->state = SONAR_WAITING;
    health->timeout = timeout;
    health->last_tick = now;
    health->next_sequence = 0U;
    health->received = 0U;
    if (timeout == 0U || timeout > UINT32_MAX / 2U) {
        health->state = SONAR_FAULT_CONFIG;
        return false;
    }
    return true;
}

static bool faulted(const sonar_health_t *health)
{
    return health->state != SONAR_WAITING && health->state != SONAR_RUNNING;
}

sonar_health_state_t sonar_health_accept(sonar_health_t *health,
                                       const sonar_heartbeat_t *event, uint32_t now)
{
    if (health == NULL) { return SONAR_FAULT_CONFIG; }
    if (faulted(health)) { return health->state; }
    if (event == NULL) {
        health->state = SONAR_FAULT_CONFIG;
    } else if (event->sequence != health->next_sequence) {
        health->state = SONAR_FAULT_SEQUENCE;
    } else if ((uint32_t)(now - health->last_tick) >= health->timeout ||
               (uint32_t)(now - event->tick) >= health->timeout ||
               (uint32_t)(event->tick - health->last_tick) >= health->timeout) {
        health->state = SONAR_FAULT_TIMEOUT;
    } else {
        health->last_tick = event->tick;
        ++health->next_sequence;
        ++health->received;
        health->state = SONAR_RUNNING;
    }
    return health->state;
}

sonar_health_state_t sonar_health_poll(sonar_health_t *health, uint32_t now)
{
    if (health == NULL) { return SONAR_FAULT_CONFIG; }
    if (!faulted(health) && (uint32_t)(now - health->last_tick) >= health->timeout) {
        health->state = SONAR_FAULT_TIMEOUT;
    }
    return health->state;
}

void sonar_health_queue_fault(sonar_health_t *health)
{
    if (health != NULL && !faulted(health)) { health->state = SONAR_FAULT_QUEUE; }
}

const char *sonar_health_name(sonar_health_state_t state)
{
    switch (state) {
    case SONAR_WAITING: return "WAITING";
    case SONAR_RUNNING: return "RUNNING";
    case SONAR_FAULT_CONFIG: return "CONFIG";
    case SONAR_FAULT_TIMEOUT: return "TIMEOUT";
    case SONAR_FAULT_SEQUENCE: return "SEQUENCE";
    case SONAR_FAULT_QUEUE: return "QUEUE_FULL";
    default: return "UNKNOWN";
    }
}
