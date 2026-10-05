#ifndef SONAR_CHIRP_H
#define SONAR_CHIRP_H
#include "sonar_experiment_config.h"
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
