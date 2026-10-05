#ifndef SONAR_SCAN_SIM_H
#define SONAR_SCAN_SIM_H
#include "sonar_scan.h"
typedef struct {
    sonar_scan_t scan;
    uint32_t now, frames, moves;
    int32_t target;
    bool armed;
} sonar_scan_sim_t;
/* No register access, audio buffer, or physical timing model. One bounded step
 * advances virtual time to the next transition; the caller owns frame release. */
bool sonar_scan_sim_init(sonar_scan_sim_t *sim, const sonar_scan_config_t *config);
void sonar_scan_sim_step(sonar_scan_sim_t *sim);
bool sonar_scan_sim_release(sonar_scan_sim_t *sim);
#endif
