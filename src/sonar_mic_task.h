#ifndef SONAR_MIC_TASK_H
#define SONAR_MIC_TASK_H
#include <stdbool.h>
bool sonar_mic_task_create(void);
/* Latch a system-health stop request; serviced by the owning capture task. */
void sonar_mic_task_inhibit(void);
#endif
