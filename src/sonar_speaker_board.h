#ifndef SONAR_SPEAKER_BOARD_H
#define SONAR_SPEAKER_BOARD_H
#include "sonar_speaker.h"

/* Component configuration enabled for the verified two-bit GPIO / AXI IIC export. */
#ifndef SONAR_SPEAKER_ENABLE_HARDWARE
#define SONAR_SPEAKER_ENABLE_HARDWARE 1
#endif

bool sonar_speaker_board_init(void);
bool sonar_speaker_board_output_ready(void);
bool sonar_speaker_board_begin(uint32_t now);
void sonar_speaker_board_poll(uint32_t now);
bool sonar_speaker_board_enable(bool enabled);
void sonar_speaker_board_cancel(void);
const sonar_speaker_t *sonar_speaker_board_state(void);
#endif
