#ifndef SONAR_RTOS_H
#define SONAR_RTOS_H

#include <stdbool.h>
#include "sonar_config.h"

/* Call once before starting the scheduler; timings must already be validated. */
bool sonar_rtos_create(const sonar_timing_t *timing);
/* Liveness signal from the experiment task. Call every loop iteration; it is
 * rate-limited to one heartbeat per period. */
void sonar_rtos_heartbeat(void);
void sonar_halt(const char *reason);

#endif
