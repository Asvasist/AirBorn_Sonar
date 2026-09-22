#ifndef SONAR_HEALTH_H
#define SONAR_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SONAR_WAITING = 0,
    SONAR_RUNNING,
    SONAR_FAULT_CONFIG,
    SONAR_FAULT_TIMEOUT,
    SONAR_FAULT_SEQUENCE,
    SONAR_FAULT_QUEUE
} sonar_health_state_t;

typedef struct {
    uint32_t sequence;
    uint32_t tick;
} sonar_heartbeat_t;

/* Single owner: only the supervisor task may change this object. */
typedef struct {
    sonar_health_state_t state;
    uint32_t timeout;
    uint32_t last_tick;
    uint32_t next_sequence;
    uint32_t received;
} sonar_health_t;

bool sonar_health_init(sonar_health_t *health, uint32_t now, uint32_t timeout);
sonar_health_state_t sonar_health_accept(sonar_health_t *health,
                                       const sonar_heartbeat_t *event, uint32_t now);
sonar_health_state_t sonar_health_poll(sonar_health_t *health, uint32_t now);
void sonar_health_queue_fault(sonar_health_t *health);
const char *sonar_health_name(sonar_health_state_t state);

#endif
