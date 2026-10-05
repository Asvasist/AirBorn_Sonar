#ifndef SONAR_FRAMES_H
#define SONAR_FRAMES_H
#include "sonar_acquisition_config.h"
#include <stdbool.h>
#include <stdint.h>
#define SONAR_WIRE_HEADER 80U
typedef struct {
    uint32_t sequence, flags, divisor;
    int32_t steps;
    int64_t position;
    uint64_t trigger_us, completion_us;
    uint32_t tx_duration_us;
    uint32_t config_id, tx_mode, tx_start_hz, tx_stop_hz, tx_amplitude_pct;
    uint32_t tx_samples, waveform_id, waveform_crc, tx_sample_hz;
} sonar_frame_meta_t;
typedef enum { FRAME_FREE, FRAME_FILLING, FRAME_READY, FRAME_SENDING } sonar_frame_state_t;
typedef struct { sonar_frame_state_t state; sonar_frame_meta_t meta; } sonar_frame_slot_t;
typedef struct { sonar_frame_slot_t slots[SONAR_ACQ_POOL_COUNT]; } sonar_frames_t;
/* Caller serializes these short metadata operations. Payload is exclusively
 * owned by capture in FILLING, and by the sender in SENDING. */
int sonar_frames_acquire(sonar_frames_t *);
bool sonar_frames_publish(sonar_frames_t *, unsigned, const sonar_frame_meta_t *);
int sonar_frames_take(sonar_frames_t *, uint32_t through_sequence);
bool sonar_frames_finish(sonar_frames_t *, unsigned, bool acknowledged);
uint32_t sonar_frames_latest(const sonar_frames_t *);
uint32_t sonar_crc32(const uint8_t *, uint32_t);
void sonar_frame_header(uint8_t out[SONAR_WIRE_HEADER], const sonar_frame_meta_t *, uint32_t bytes, uint32_t crc);
#endif
