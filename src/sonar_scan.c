#include "sonar_scan.h"
#include <stddef.h>

static void next_generation(sonar_scan_t *scan)
{ ++scan->generation; if (scan->generation == 0U) { ++scan->generation; } }
void sonar_scan_fault(sonar_scan_t *scan, sonar_scan_fault_t fault)
{
    if (scan == NULL || scan->state == SCAN_FAULT || scan->state == SCAN_COMPLETE) { return; }
    scan->state = SCAN_FAULT;
    scan->fault = fault == SCAN_OK ? SCAN_IO_ERROR : fault;
    scan->stopped_mask = scan->io.abort(scan->io.context) & SCAN_STOP_ALL;
}
bool sonar_scan_init(sonar_scan_t *scan, const sonar_scan_config_t *config, uint32_t tick_hz,
    const sonar_scan_io_t *io)
{
    sonar_scan_timing_t timing;
    if (scan == NULL || io == NULL || io->move == NULL || io->arm_rx == NULL || io->trigger == NULL ||
        io->release_rx == NULL || io->stop == NULL || io->abort == NULL ||
        sonar_scan_config_validate(config, tick_hz, &timing) != SCAN_CONFIG_OK) { return false; }
    *scan = (sonar_scan_t){.config = *config, .timing = timing, .io = *io, .state = SCAN_IDLE};
    return true;
}
static bool request_motion(sonar_scan_t *scan, uint32_t now)
{
    int32_t steps = 0, angle = 0;
    if (scan->config.mode == SCAN_STEP_AND_STOP && !sonar_scan_target(&scan->config, scan->index, &steps, &angle)) {
        sonar_scan_fault(scan, SCAN_IO_ERROR); return false;
    }
    scan->state = SCAN_MOVING; scan->state_tick = now;
    if (!scan->io.move(scan->io.context, scan->config.mode, steps, scan->config.velocity, scan->generation)) {
        sonar_scan_fault(scan, SCAN_IO_ERROR); return false;
    }
    return true;
}
bool sonar_scan_start(sonar_scan_t *scan, uint32_t now)
{
    if (scan == NULL || scan->state != SCAN_IDLE) { return false; }
    next_generation(scan);
    return request_motion(scan, now);
}
static void capture(sonar_scan_t *scan, uint32_t now)
{
    scan->frame = (sonar_measurement_t){.index = scan->index, .generation = scan->generation,
        .bytes = scan->config.capture_bytes, .trigger_tick = now};
    if (scan->config.mode == SCAN_STEP_AND_STOP) {
        scan->frame.requested_angle_valid = sonar_scan_target(&scan->config, scan->index,
            &scan->frame.target_steps, &scan->frame.requested_angle_mdeg);
    }
    if (scan->io.position != NULL) {
        scan->frame.position_available = scan->io.position(scan->io.context, &scan->frame.commanded_steps_at_trigger);
    }
    scan->completed_flags = 0U;
    scan->state = SCAN_CAPTURING; scan->state_tick = now;
    /* Reception must own its buffer before the common hardware trigger. */
    if (!scan->io.arm_rx(scan->io.context, scan->config.capture_bytes, scan->generation) ||
        !scan->io.trigger(scan->io.context, scan->generation)) { sonar_scan_fault(scan, SCAN_IO_ERROR); return; }
    scan->has_triggered = true; scan->trigger_tick = now;
}
void sonar_scan_poll(sonar_scan_t *scan, uint32_t now)
{
    if (scan == NULL) { return; }
    uint32_t elapsed = now - scan->state_tick, deadline = 0U;
    switch (scan->state) {
    case SCAN_MOVING: deadline = scan->timing.motion_timeout; break;
    case SCAN_CAPTURING: deadline = scan->timing.capture_timeout; break;
    case SCAN_STOPPING: deadline = scan->timing.stop_timeout; break;
    case SCAN_FRAME_READY: deadline = scan->timing.frame_hold_timeout; break;
    case SCAN_SETTLING:
        if (elapsed >= scan->timing.settle) { scan->state = SCAN_WAIT_INTERVAL; }
        break;
    default: break;
    }
    if (deadline != 0U && elapsed >= deadline) { sonar_scan_fault(scan, SCAN_DEADLINE); return; }
    if (scan->state == SCAN_WAIT_INTERVAL && (!scan->has_triggered ||
        (uint32_t)(now - scan->trigger_tick) >= scan->timing.interval)) { capture(scan, now); }
}
void sonar_scan_event(sonar_scan_t *scan, const sonar_scan_event_t *event, uint32_t now)
{
    if (scan == NULL || event == NULL) { return; }
    if (scan->state == SCAN_IDLE || scan->state == SCAN_FAULT || scan->state == SCAN_COMPLETE ||
        event->generation != scan->generation) { return; }
    if ((event->flags & SCAN_EVENT_ERROR) != 0U) { sonar_scan_fault(scan, SCAN_IO_ERROR); return; }
    if ((event->flags & SCAN_EVENT_OVERFLOW) != 0U) { sonar_scan_fault(scan, SCAN_OVERFLOW); return; }
    /* An incoming event must never start a new transfer while being classified. */
    if (scan->state == SCAN_MOVING || scan->state == SCAN_CAPTURING ||
        scan->state == SCAN_STOPPING || scan->state == SCAN_FRAME_READY) {
        sonar_scan_poll(scan, now);
    }
    uint32_t accepted = scan->state == SCAN_MOVING ? SCAN_EVENT_MOTION_DONE :
        scan->state == SCAN_STOPPING ? SCAN_EVENT_STOPPED :
        scan->state == SCAN_CAPTURING ? SCAN_EVENT_RX_DONE | SCAN_EVENT_TX_DONE : 0U;
    if ((event->flags & accepted) == 0U) { return; }
    if ((uint32_t)(event->tick - scan->state_tick) > (uint32_t)(now - scan->state_tick)) {
        sonar_scan_fault(scan, SCAN_EVENT_TIME); return;
    }
    if (scan->state == SCAN_MOVING) { scan->state = SCAN_SETTLING; scan->state_tick = now; return; }
    if (scan->state == SCAN_STOPPING) { scan->state = SCAN_COMPLETE; scan->stopped_mask = SCAN_STOP_ALL; return; }
    if ((event->flags & SCAN_EVENT_RX_DONE) != 0U && (scan->completed_flags & SCAN_EVENT_RX_DONE) == 0U) {
        if (event->bytes != scan->config.capture_bytes) { sonar_scan_fault(scan, SCAN_CAPTURE_LENGTH); return; }
        if (!event->overflow_observable) { sonar_scan_fault(scan, SCAN_INTEGRITY_UNKNOWN); return; }
        scan->frame.rx_done_tick = event->tick; scan->frame.overflow_checked = true;
        scan->completed_flags |= SCAN_EVENT_RX_DONE;
    }
    if ((event->flags & SCAN_EVENT_TX_DONE) != 0U && (scan->completed_flags & SCAN_EVENT_TX_DONE) == 0U) {
        scan->frame.tx_done_tick = event->tick; scan->completed_flags |= SCAN_EVENT_TX_DONE;
    }
    if (scan->completed_flags == (SCAN_EVENT_RX_DONE | SCAN_EVENT_TX_DONE)) {
        scan->state = SCAN_FRAME_READY; scan->state_tick = now;
    }
}
bool sonar_scan_frame(const sonar_scan_t *scan, sonar_measurement_t *frame)
{
    if (scan == NULL || frame == NULL || scan->state != SCAN_FRAME_READY) { return false; }
    *frame = scan->frame; return true;
}
bool sonar_scan_release(sonar_scan_t *scan, uint32_t now)
{
    if (scan == NULL) { return false; }
    sonar_scan_poll(scan, now);
    if (scan->state != SCAN_FRAME_READY) { return false; }
    if (!scan->io.release_rx(scan->io.context, scan->generation)) { sonar_scan_fault(scan, SCAN_IO_ERROR); return false; }
    ++scan->index; next_generation(scan);
    if (scan->index == scan->config.shots) {
        scan->state = SCAN_STOPPING; scan->state_tick = now;
        if (!scan->io.stop(scan->io.context, scan->generation)) { sonar_scan_fault(scan, SCAN_IO_ERROR); return false; }
    } else if (scan->config.mode == SCAN_STEP_AND_STOP) { return request_motion(scan, now); }
    else { scan->state = SCAN_WAIT_INTERVAL; scan->state_tick = now; }
    return true;
}
void sonar_scan_cancel(sonar_scan_t *scan)
{
    if (scan != NULL && scan->state != SCAN_IDLE) { sonar_scan_fault(scan, SCAN_CANCELLED); }
}
const char *sonar_scan_state_name(sonar_scan_state_t state)
{
    static const char *const names[] = {"IDLE","MOVING","SETTLING","WAIT_INTERVAL",
        "CAPTURING","FRAME_READY","STOPPING","COMPLETE","FAULT"};
    return (unsigned)state < sizeof(names)/sizeof(names[0]) ? names[state] : "UNKNOWN";
}
