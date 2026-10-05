#ifndef SONAR_EXPERIMENT_H
#define SONAR_EXPERIMENT_H
#include "sonar_experiment_config.h"
#include "sonar_control_protocol.h"
/* Experiment task only. Network task communicates through copied queues. */
void sonar_experiment_command(uint32_t session, uint32_t opcode, const uint8_t *data,
                              uint32_t bytes, bool mutable,
                              uint32_t reply[SONAR_CONTROL_REPLY_WORDS]);
void sonar_experiment_reply(uint32_t error, uint32_t reply[SONAR_CONTROL_REPLY_WORDS]);
bool sonar_experiment_can_start(void);
void sonar_experiment_running(bool running);
const sonar_experiment_config_t *sonar_experiment_active(void);
const sonar_chirp_values_t *sonar_experiment_values(void);
#endif
