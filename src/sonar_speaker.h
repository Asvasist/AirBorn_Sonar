#ifndef SONAR_SPEAKER_H
#define SONAR_SPEAKER_H

#include <stdbool.h>
#include <stdint.h>

#define SONAR_CODEC_ADDRESS UINT8_C(0x1a)
#define SONAR_CODEC_WRITES 10U
typedef enum { SPEAKER_OFF, SPEAKER_CONFIGURING, SPEAKER_READY, SPEAKER_FAULT } sonar_speaker_state_t;
typedef enum { SPEAKER_OK, SPEAKER_MUTE_FAILED, SPEAKER_WRITE_FAILED,
    SPEAKER_BUS_ERROR, SPEAKER_TIMEOUT, SPEAKER_CANCELLED } sonar_speaker_fault_t;
typedef enum { SPEAKER_WAIT_POWER, SPEAKER_WAIT_ACK, SPEAKER_WAIT_VMID } sonar_speaker_phase_t;

typedef struct {
    uint32_t powerup_ticks, vmid_ticks, write_timeout_ticks;
    uint8_t volume; /* SSM2603 register field, 0..127; not a percentage. */
} sonar_speaker_config_t;

/* Nonblocking write: copy both bytes before returning and tag completion with
 * token. Never call the service recursively from a callback or an ISR.
 * abort() returns true only after the controller has relinquished the bus.
 * enable(false) must preserve the shared GPIO trigger bit. It mutes the codec
 * output; it does not stop the FPGA playback state machine. */
typedef struct {
    void *context;
    bool (*write)(void *context, uint8_t address, const uint8_t bytes[2], uint32_t token);
    bool (*abort)(void *context);
    bool (*enable)(void *context, bool enabled);
} sonar_speaker_io_t;

typedef struct {
    sonar_speaker_io_t io;
    sonar_speaker_config_t config;
    sonar_speaker_state_t state;
    sonar_speaker_fault_t fault;
    sonar_speaker_phase_t phase;
    uint32_t phase_tick, token;
    uint8_t index;
    bool bus_owned, output_enabled, mute_confirmed;
} sonar_speaker_t;

/* The same 16-bit I2S/48 kHz sequence as the supplied codec initializer.
 * Timing is rounded up from its 10 ms power-up and 80 ms VMID delays.
 * Bus deadline is a software policy of 100 ms per transaction. */
bool sonar_speaker_default_config(uint32_t tick_hz, sonar_speaker_config_t *config);
bool sonar_speaker_encode(uint8_t reg, uint16_t value, uint8_t bytes[2]);
/* Single task ownership; initialize once, only with an idle bus/muted output. */
bool sonar_speaker_init(sonar_speaker_t *speaker, const sonar_speaker_config_t *config,
                        const sonar_speaker_io_t *io);
bool sonar_speaker_begin(sonar_speaker_t *speaker, uint32_t now);
void sonar_speaker_poll(sonar_speaker_t *speaker, uint32_t now);
/* acknowledged means both data bytes ACKed and STOP completed. */
void sonar_speaker_event(sonar_speaker_t *speaker, uint32_t token, bool acknowledged, uint32_t now);
bool sonar_speaker_enable(sonar_speaker_t *speaker, bool enabled);
void sonar_speaker_cancel(sonar_speaker_t *speaker);

/* READY proves configuration writes completed, not audible playback. The
 * active BD shares TX/RX start and exposes no processor-readable TX completion,
 * so this service deliberately has no independent play()/playback_done(). */
#endif
