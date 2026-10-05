#include "sonar_scan_sim.h"
#include <stddef.h>
static bool move(void *context, sonar_scan_mode_t mode, int32_t target, int32_t velocity, uint32_t generation)
{
    sonar_scan_sim_t *sim = context;
    (void)mode; (void)velocity; (void)generation;
    sim->target = target; ++sim->moves; return true;
}
static bool arm(void *context, uint32_t bytes, uint32_t generation)
{
    sonar_scan_sim_t *sim = context; (void)bytes; (void)generation;
    if (sim->armed) { return false; } sim->armed = true; return true;
}
static bool trigger(void *context, uint32_t generation)
{ sonar_scan_sim_t *sim = context; (void)generation; return sim->armed; }
static bool release(void *context, uint32_t generation)
{
    sonar_scan_sim_t *sim = context; (void)generation;
    if (!sim->armed) { return false; } sim->armed = false; ++sim->frames; return true;
}
static bool stop(void *context, uint32_t generation)
{ (void)context; (void)generation; return true; }
static uint32_t abort_scan(void *context)
{ sonar_scan_sim_t *sim = context; sim->armed = false; return SCAN_STOP_ALL; }
static bool position(void *context, int32_t *steps)
{
    sonar_scan_sim_t *sim = context;
    if (sim->scan.config.mode == SCAN_CONTINUOUS) { return false; }
    *steps = sim->target; return true;
}
bool sonar_scan_sim_init(sonar_scan_sim_t *sim, const sonar_scan_config_t *config)
{
    if (sim == NULL) { return false; }
    *sim = (sonar_scan_sim_t){0};
    const sonar_scan_io_t io = {sim, move, arm, trigger, release, stop, abort_scan, position};
    return sonar_scan_init(&sim->scan, config, 1000U, &io) && sonar_scan_start(&sim->scan, 0U);
}
void sonar_scan_sim_step(sonar_scan_sim_t *sim)
{
    if (sim == NULL) { return; }
    sonar_scan_t *scan = &sim->scan;
    uint32_t flags = 0U;
    switch (scan->state) {
    case SCAN_MOVING: flags = SCAN_EVENT_MOTION_DONE; ++sim->now; break;
    case SCAN_STOPPING: flags = SCAN_EVENT_STOPPED; ++sim->now; break;
    case SCAN_CAPTURING: flags = SCAN_EVENT_RX_DONE | SCAN_EVENT_TX_DONE; ++sim->now; break;
    case SCAN_SETTLING: sim->now = scan->state_tick + scan->timing.settle; break;
    case SCAN_WAIT_INTERVAL: {
        uint32_t elapsed = sim->now - scan->trigger_tick;
        if (scan->has_triggered && elapsed < scan->timing.interval) { sim->now += scan->timing.interval - elapsed; }
        break;
    }
    case SCAN_FRAME_READY: ++sim->now; break;
    default: return;
    }
    if (flags != 0U) {
        const sonar_scan_event_t event = {scan->generation, flags, scan->config.capture_bytes, sim->now, true};
        sonar_scan_event(scan, &event, sim->now);
    } else { sonar_scan_poll(scan, sim->now); }
}
bool sonar_scan_sim_release(sonar_scan_sim_t *sim)
{ return sim != NULL && sonar_scan_release(&sim->scan, sim->now); }
