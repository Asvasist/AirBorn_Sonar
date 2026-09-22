#ifndef SONAR_SCAN_H
#define SONAR_SCAN_H
#include "sonar_scan_config.h"

typedef enum { SCAN_IDLE, SCAN_MOVING, SCAN_SETTLING, SCAN_WAIT_INTERVAL,
    SCAN_CAPTURING, SCAN_FRAME_READY, SCAN_STOPPING, SCAN_COMPLETE, SCAN_FAULT } sonar_scan_state_t;
typedef enum { SCAN_OK, SCAN_IO_ERROR, SCAN_DEADLINE, SCAN_CAPTURE_LENGTH,
    SCAN_OVERFLOW, SCAN_INTEGRITY_UNKNOWN, SCAN_CANCELLED, SCAN_EVENT_TIME } sonar_scan_fault_t;
#define SCAN_EVENT_MOTION_DONE UINT32_C(1)
#define SCAN_EVENT_RX_DONE UINT32_C(2)
#define SCAN_EVENT_TX_DONE UINT32_C(4)
#define SCAN_EVENT_STOPPED UINT32_C(8)
#define SCAN_EVENT_ERROR UINT32_C(16)
#define SCAN_EVENT_OVERFLOW UINT32_C(32)
#define SCAN_STOP_MOTOR UINT32_C(1)
#define SCAN_STOP_RX UINT32_C(2)
#define SCAN_STOP_TX UINT32_C(4)
#define SCAN_STOP_ALL UINT32_C(7)

typedef struct {
    void *context;
    bool (*move)(void *context, sonar_scan_mode_t mode, int32_t target_steps, int32_t velocity, uint32_t generation);
    bool (*arm_rx)(void *context, uint32_t bytes, uint32_t generation);
    bool (*trigger)(void *context, uint32_t generation);
    bool (*release_rx)(void *context, uint32_t generation);
    bool (*stop)(void *context, uint32_t generation);
    uint32_t (*abort)(void *context); /* Confirmed stopped components, SCAN_STOP_* mask. */
    bool (*position)(void *context, int32_t *commanded_steps);
} sonar_scan_io_t;
typedef struct {
    uint32_t generation, flags, bytes, tick;
    bool overflow_observable;
} sonar_scan_event_t;
typedef struct {
    uint32_t index, generation, bytes, trigger_tick, rx_done_tick, tx_done_tick;
    int32_t requested_angle_mdeg, target_steps, commanded_steps_at_trigger;
    bool requested_angle_valid, position_available, overflow_checked;
} sonar_measurement_t;
typedef struct {
    sonar_scan_config_t config;
    sonar_scan_timing_t timing;
    sonar_scan_io_t io;
    sonar_scan_state_t state;
    sonar_scan_fault_t fault;
    sonar_measurement_t frame;
    uint32_t generation, index, state_tick, trigger_tick, completed_flags, stopped_mask;
    bool has_triggered;
} sonar_scan_t;

/* One task owns the coordinator, its component callbacks and all event delivery.
 * Callbacks are bounded and never re-enter the coordinator. Accepted movement
 * and stop requests finish only through tagged events; component drivers must
 * keep servicing their watchdogs while frames are held. */
bool sonar_scan_init(sonar_scan_t *scan, const sonar_scan_config_t *config, uint32_t tick_hz,
    const sonar_scan_io_t *io);
bool sonar_scan_start(sonar_scan_t *scan, uint32_t now);
void sonar_scan_poll(sonar_scan_t *scan, uint32_t now);
void sonar_scan_event(sonar_scan_t *scan, const sonar_scan_event_t *event, uint32_t now);
bool sonar_scan_frame(const sonar_scan_t *scan, sonar_measurement_t *frame);
bool sonar_scan_release(sonar_scan_t *scan, uint32_t now);
void sonar_scan_cancel(sonar_scan_t *scan);
void sonar_scan_fault(sonar_scan_t *scan, sonar_scan_fault_t fault);
const char *sonar_scan_state_name(sonar_scan_state_t state);

/* Measurements contain metadata only. The capture adapter retains the actual
 * buffer until release_rx; it must not rearm or overwrite a borrowed frame.
 * Tick timestamps and commanded steps are not sample timestamps/encoder data. */
#endif
