#ifndef SONAR_STAGE2_H
#define SONAR_STAGE2_H
#include "sonar_frames.h"
bool sonar_stage2_create(void);
void sonar_stage2_inhibit(void);
void sonar_stage2_key(uint8_t);
void sonar_network_task(void *);
bool sonar_network_connected(void);
/* Metadata lock only; never hold a critical section during DMA or Ethernet I/O. */
extern sonar_frames_t sonar_frames;
extern uint8_t sonar_frame_data[SONAR_STAGE2_POOL_COUNT][SONAR_STAGE2_SPAN];
#endif
