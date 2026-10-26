#ifndef SONAR_TX_CONTROL_H
#define SONAR_TX_CONTROL_H
#include "sonar_experiment_config.h"
/* Experiment-task owner only, including prepare -> shared GPIO trigger ->
 * triggered. TCP requests already enter through the owner's control queue.
 * A TX fault is latched: do not reset/reinitialize audio mid-transaction. */
bool sonar_chirp_init(void);
uint32_t sonar_chirp_capabilities(void);
uint32_t sonar_chirp_status(void);
bool sonar_chirp_apply(const sonar_experiment_config_t *, const sonar_chirp_values_t *);
bool sonar_chirp_prepare(void);
void sonar_chirp_triggered(uint64_t trigger_us);
bool sonar_chirp_done(void);
bool sonar_chirp_error(void);
bool sonar_chirp_abort(void);
#endif
