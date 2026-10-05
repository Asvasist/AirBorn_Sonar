#ifndef SONAR_WAV_PLAYER_H
#define SONAR_WAV_PLAYER_H
#include <stdbool.h>
#include <stdint.h>

bool sonar_wav_player_init(void);
bool sonar_wav_player_available(void);
bool sonar_wav_player_has_gain(void);
bool sonar_wav_player_has_done(void);
bool sonar_wav_player_select_generate(void);
bool sonar_wav_player_configure(uint32_t samples, uint32_t amplitude_pct);
bool sonar_wav_player_prepare(void);
bool sonar_wav_player_done(void);
bool sonar_wav_player_error(void);
bool sonar_wav_player_busy(void);
/* True only after an implemented abort command has completed. */
bool sonar_wav_player_abort(void);
#endif
