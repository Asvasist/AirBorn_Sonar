#ifndef SONAR_CONTROL_SERVER_H
#define SONAR_CONTROL_SERVER_H
#include <stdbool.h>
bool sonar_control_create(void); /* Queues, before scheduler starts. */
bool sonar_control_server_start(void); /* After the one existing lwIP init. */
void sonar_control_poll(bool mutable); /* Experiment task, once each loop. */
#endif
