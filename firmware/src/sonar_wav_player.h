#ifndef SONAR_WAV_PLAYER_H
#define SONAR_WAV_PLAYER_H
#include <stdbool.h>
#include <stdint.h>

/* BRAM waveform playback. The FPGA rewinds the player on every trigger. */
bool sonar_wav_player_init(void);
bool sonar_wav_player_available(void);
bool sonar_wav_player_select_generate(void);
/* Scale the uploaded waveform to amplitude_pct and select BRAM playback. */
bool sonar_wav_player_configure(uint32_t samples, uint32_t amplitude_pct);
#endif
