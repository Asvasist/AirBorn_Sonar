#ifndef SONAR_CYCLE_H
#define SONAR_CYCLE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { CYCLE_IDLE, CYCLE_WAIT_BUFFER, CYCLE_ACQUIRE,
    CYCLE_PRE_MOVE, CYCLE_MOVING, CYCLE_SETTLING, CYCLE_STOPPED, CYCLE_FAULT } sonar_cycle_state_t;
typedef struct {
    uint64_t tx_timeout_us, motor_start_us, settle_us, capture_timeout_us, motion_timeout_us;
} sonar_cycle_config_t;
typedef struct {
    void *context;
    /* 0: no free buffer; 1: DMA armed and trigger sent; -1: failure. */
    int (*capture)(void *, uint32_t cycle, uint64_t *trigger_us);
    bool (*move)(void *, int32_t steps);
    bool (*stop)(void *);
} sonar_cycle_io_t;
typedef struct {
    sonar_cycle_config_t config;
    sonar_cycle_io_t io;
    sonar_cycle_state_t state;
    uint64_t since, trigger;
    uint64_t last_poll;
    const char *fault_reason;
    sonar_cycle_state_t fault_state;
    uint32_t cycle;
    int32_t steps;
    bool stopping, received, transmitted;
} sonar_cycle_t;
bool sonar_cycle_init(sonar_cycle_t *, const sonar_cycle_config_t *, const sonar_cycle_io_t *);
bool sonar_cycle_start(sonar_cycle_t *, int32_t steps, uint64_t now);
void sonar_cycle_poll(sonar_cycle_t *, uint64_t now, bool captured, bool transmitted, bool motor_done, bool fault);
bool sonar_cycle_stop(sonar_cycle_t *, uint64_t now);
const char *sonar_cycle_name(sonar_cycle_state_t);
#endif
