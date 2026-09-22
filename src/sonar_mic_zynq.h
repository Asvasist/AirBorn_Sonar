#ifndef SONAR_MIC_ZYNQ_H
#define SONAR_MIC_ZYNQ_H

#include "sonar_mic.h"
#include "FreeRTOS.h"
#include "task.h"

/* Call from the capture task after the scheduler starts, once per boot. */
bool sonar_mic_zynq_init(TaskHandle_t owner, sonar_mic_io_t *io);
bool sonar_mic_zynq_take_event(sonar_mic_event_t *event);

#endif
