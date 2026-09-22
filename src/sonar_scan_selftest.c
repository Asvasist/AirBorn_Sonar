#include "sonar_scan_selftest.h"
#include "sonar_scan.h"
#include "sonar_scan_command.h"
#include "sonar_scan_sim.h"
#include <stddef.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { return false; } } while (0)

typedef struct { char trace[64], fail; unsigned count; uint32_t stopped; int32_t target; } fake_t;
typedef struct { sonar_scan_t scan; sonar_scan_config_t config; fake_t fake; } fixture_t;
static bool call(fake_t *f, char name)
{
    if (f->count < sizeof(f->trace)) { f->trace[f->count++] = name; }
    return f->fail != name;
}
static bool move(void *context, sonar_scan_mode_t mode, int32_t target, int32_t velocity, uint32_t generation)
{ fake_t *f = context; (void)mode; (void)velocity; (void)generation; f->target = target; return call(f, 'M'); }
static bool arm(void *context, uint32_t bytes, uint32_t generation)
{ (void)bytes; (void)generation; return call(context, 'A'); }
static bool trigger(void *context, uint32_t generation) { (void)generation; return call(context, 'T'); }
static bool release(void *context, uint32_t generation) { (void)generation; return call(context, 'R'); }
static bool stop(void *context, uint32_t generation) { (void)generation; return call(context, 'S'); }
static uint32_t abort_all(void *context) { fake_t *f = context; (void)call(f, 'X'); return f->stopped; }
static bool position(void *context, int32_t *steps)
{ fake_t *f = context; *steps = f->target; return f->fail != 'P'; }
static bool setup(fixture_t *f, sonar_scan_mode_t mode, uint32_t shots)
{
    memset(f, 0, sizeof(*f)); f->fake.stopped = SCAN_STOP_ALL;
    f->config = sonar_scan_config_default();
    f->config.mode = mode; f->config.shots = shots;
    f->config.steps_per_revolution = 3600U; f->config.reference_confirmed = true;
    f->config.step_angle_mdeg = 10000; f->config.interval_ms = 10U; f->config.settle_ms = 2U;
    f->config.motion_timeout_ms = 100U; f->config.capture_timeout_ms = 100U;
    f->config.stop_timeout_ms = 100U; f->config.frame_hold_timeout_ms = 20U;
    const sonar_scan_io_t io = {&f->fake, move, arm, trigger, release, stop, abort_all, position};
    return sonar_scan_init(&f->scan, &f->config, 1000U, &io);
}
static void event(fixture_t *f, uint32_t flags, uint32_t tick)
{
    sonar_scan_event_t e = {f->scan.generation, flags, f->config.capture_bytes, tick, true};
    sonar_scan_event(&f->scan, &e, tick);
}
static bool begin_capture(fixture_t *f)
{
    CHECK(sonar_scan_start(&f->scan, 0U)); event(f, SCAN_EVENT_MOTION_DONE, 1U);
    sonar_scan_poll(&f->scan, 3U); return f->scan.state == SCAN_CAPTURING;
}
static bool finish_frame(fixture_t *f)
{
    CHECK(begin_capture(f)); event(f, SCAN_EVENT_RX_DONE, 4U); event(f, SCAN_EVENT_TX_DONE, 5U);
    return f->scan.state == SCAN_FRAME_READY;
}
static bool defaults_require_calibration(void)
{
    sonar_scan_config_t config = sonar_scan_config_default(); sonar_scan_timing_t timing;
    CHECK(sonar_scan_config_validate(&config, 1000U, &timing) == SCAN_CONFIG_CALIBRATION);
    config.mode = SCAN_CONTINUOUS;
    return sonar_scan_config_validate(&config, 1000U, &timing) == SCAN_CONFIG_OK;
}
static bool angle_conversion_and_rounding(void)
{
    sonar_scan_config_t config = sonar_scan_config_default(); int32_t steps, angle;
    config.steps_per_revolution = 3200U; config.reference_steps = 50;
    config.start_angle_mdeg = 90000; config.step_angle_mdeg = -45000;
    CHECK(sonar_scan_target(&config, 0U, &steps, &angle) && steps == 850 && angle == 90000);
    CHECK(sonar_scan_target(&config, 1U, &steps, &angle) && steps == 450 && angle == 45000);
    config.start_angle_mdeg = -57; config.reference_steps = 0;
    return sonar_scan_target(&config, 0U, &steps, &angle) && steps == -1;
}
static bool target_overflow_rejected(void)
{
    fixture_t f; sonar_scan_timing_t timing; CHECK(setup(&f, SCAN_STEP_AND_STOP, 3U));
    f.config.start_angle_mdeg = INT32_MAX; f.config.step_angle_mdeg = INT32_MAX;
    CHECK(sonar_scan_config_validate(&f.config, 1000U, &timing) == SCAN_CONFIG_TARGET_RANGE);
    f.config.start_angle_mdeg = 360000; f.config.step_angle_mdeg = 0;
    f.config.reference_steps = INT32_MAX;
    return sonar_scan_config_validate(&f.config, 1000U, &timing) == SCAN_CONFIG_TARGET_RANGE;
}
static bool invalid_limits_and_time_preserve_output(void)
{
    fixture_t f; sonar_scan_timing_t timing = {7U,7U,7U,7U,7U,7U}; CHECK(setup(&f, SCAN_CONTINUOUS, 1U));
    f.config.capture_bytes = 3U; CHECK(sonar_scan_config_validate(&f.config, 1000U, &timing) == SCAN_CONFIG_LIMIT);
    f.config.capture_bytes = 15000U; f.config.interval_ms = 0U;
    CHECK(sonar_scan_config_validate(&f.config, 1000U, &timing) == SCAN_CONFIG_TIMING);
    return timing.interval == 7U;
}
static bool acquisition_order(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    return f.fake.count == 3U && memcmp(f.fake.trace, "MAT", 3U) == 0;
}
static bool settling_is_respected(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(sonar_scan_start(&f.scan, 0U));
    sonar_scan_poll(&f.scan, 50U); CHECK(f.fake.count == 1U);
    event(&f, SCAN_EVENT_MOTION_DONE, 51U); sonar_scan_poll(&f.scan, 52U);
    CHECK(f.fake.count == 1U); sonar_scan_poll(&f.scan, 53U); return f.fake.count == 3U;
}
static bool both_completions_required(void)
{
    fixture_t f; sonar_measurement_t frame; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(begin_capture(&f));
    event(&f, SCAN_EVENT_RX_DONE, 4U); CHECK(!sonar_scan_frame(&f.scan, &frame));
    event(&f, SCAN_EVENT_TX_DONE, 5U); CHECK(sonar_scan_frame(&f.scan, &frame));
    return frame.rx_done_tick == 4U && frame.tx_done_tick == 5U && frame.trigger_tick == 3U && frame.overflow_checked;
}
static bool reverse_completion_order(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(begin_capture(&f));
    event(&f, SCAN_EVENT_TX_DONE, 4U); CHECK(f.scan.state == SCAN_CAPTURING);
    event(&f, SCAN_EVENT_RX_DONE, 5U); return f.scan.state == SCAN_FRAME_READY;
}
static bool held_frame_prevents_rearm(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 2U)); CHECK(finish_frame(&f));
    sonar_scan_poll(&f.scan, 15U); CHECK(f.fake.count == 3U);
    return !sonar_scan_start(&f.scan, 16U) && f.scan.state == SCAN_FRAME_READY;
}
static bool continuous_cadence_and_single_motor_start(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 2U)); CHECK(finish_frame(&f));
    CHECK(sonar_scan_release(&f.scan, 6U)); sonar_scan_poll(&f.scan, 12U);
    CHECK(f.fake.count == 4U); sonar_scan_poll(&f.scan, 13U);
    return memcmp(f.fake.trace, "MATRAT", 6U) == 0 && f.scan.generation == 2U;
}
static bool step_mode_moves_each_angle(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 2U)); CHECK(finish_frame(&f));
    CHECK(sonar_scan_release(&f.scan, 6U)); CHECK(f.fake.target == 100);
    event(&f, SCAN_EVENT_MOTION_DONE, 7U); sonar_scan_poll(&f.scan, 13U);
    return memcmp(f.fake.trace, "MATRMAT", 7U) == 0;
}
static bool final_stop_requires_event(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(finish_frame(&f));
    CHECK(sonar_scan_release(&f.scan, 6U)); CHECK(f.scan.state == SCAN_STOPPING);
    event(&f, SCAN_EVENT_STOPPED, 7U); return f.scan.state == SCAN_COMPLETE && f.scan.stopped_mask == SCAN_STOP_ALL;
}
static bool stale_events_ignored(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    sonar_scan_event_t e = {0U, SCAN_EVENT_ERROR, 0U, 4U, true};
    sonar_scan_event(&f.scan, &e, 4U); return f.scan.state == SCAN_CAPTURING;
}
static bool duplicate_rx_keeps_first_timestamp(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    event(&f, SCAN_EVENT_RX_DONE, 4U); event(&f, SCAN_EVENT_RX_DONE, 5U); event(&f, SCAN_EVENT_TX_DONE, 6U);
    return f.scan.frame.rx_done_tick == 4U;
}
static bool error_wins_over_completion(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    event(&f, SCAN_EVENT_ERROR | SCAN_EVENT_RX_DONE | SCAN_EVENT_TX_DONE, 4U);
    return f.scan.state == SCAN_FAULT && f.scan.fault == SCAN_IO_ERROR;
}
static bool exact_capture_length(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    sonar_scan_event_t e = {f.scan.generation, SCAN_EVENT_RX_DONE, 14996U, 4U, true};
    sonar_scan_event(&f.scan, &e, 4U); return f.scan.fault == SCAN_CAPTURE_LENGTH;
}
static bool overflow_is_not_a_valid_frame(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    event(&f, SCAN_EVENT_OVERFLOW | SCAN_EVENT_RX_DONE, 4U); return f.scan.fault == SCAN_OVERFLOW;
}
static bool missing_integrity_is_explicit(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    sonar_scan_event_t e = {f.scan.generation, SCAN_EVENT_RX_DONE, 15000U, 4U, false};
    sonar_scan_event(&f.scan, &e, 4U); return f.scan.fault == SCAN_INTEGRITY_UNKNOWN;
}
static bool failed_callbacks_abort(void)
{
    const char failures[] = {'M','A','T'};
    for (unsigned i = 0U; i < sizeof(failures); ++i) {
        fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); f.fake.fail = failures[i];
        (void)sonar_scan_start(&f.scan, 0U); event(&f, SCAN_EVENT_MOTION_DONE, 1U); sonar_scan_poll(&f.scan, 3U);
        CHECK(f.scan.state == SCAN_FAULT && f.fake.trace[f.fake.count - 1U] == 'X');
    }
    return true;
}
static bool failed_release_or_stop_aborts(void)
{
    const char failures[] = {'R','S'};
    for (unsigned i = 0U; i < sizeof(failures); ++i) {
        fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(finish_frame(&f)); f.fake.fail = failures[i];
        CHECK(!sonar_scan_release(&f.scan, 6U)); CHECK(f.scan.state == SCAN_FAULT);
    }
    return true;
}
static bool inclusive_motion_deadline(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(sonar_scan_start(&f.scan, 0U));
    event(&f, SCAN_EVENT_MOTION_DONE, 100U); return f.scan.fault == SCAN_DEADLINE;
}
static bool missing_tx_times_out(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    event(&f, SCAN_EVENT_RX_DONE, 4U); sonar_scan_poll(&f.scan, 103U); return f.scan.fault == SCAN_DEADLINE;
}
static bool held_frame_deadline(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(finish_frame(&f));
    sonar_scan_poll(&f.scan, 25U); return f.scan.fault == SCAN_DEADLINE && f.fake.trace[3] == 'X';
}
static bool missing_stop_times_out(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(finish_frame(&f));
    CHECK(sonar_scan_release(&f.scan, 6U)); sonar_scan_poll(&f.scan, 106U); return f.scan.fault == SCAN_DEADLINE;
}
static bool future_event_rejected(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_STEP_AND_STOP, 1U)); CHECK(begin_capture(&f));
    sonar_scan_event_t e = {f.scan.generation, SCAN_EVENT_RX_DONE, 15000U, 5U, true};
    sonar_scan_event(&f.scan, &e, 4U); return f.scan.fault == SCAN_EVENT_TIME;
}
static bool cancellation_retains_uncertainty(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(begin_capture(&f));
    f.fake.stopped = SCAN_STOP_RX; sonar_scan_cancel(&f.scan); unsigned count = f.fake.count;
    sonar_scan_cancel(&f.scan); event(&f, SCAN_EVENT_RX_DONE, 4U);
    return f.scan.fault == SCAN_CANCELLED && f.scan.stopped_mask == SCAN_STOP_RX && f.fake.count == count;
}
static bool timing_wrap(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(sonar_scan_start(&f.scan, UINT32_MAX - 49U));
    sonar_scan_poll(&f.scan, 49U); CHECK(f.scan.state == SCAN_MOVING);
    sonar_scan_poll(&f.scan, 50U); return f.scan.fault == SCAN_DEADLINE;
}
static bool unavailable_position_is_labelled(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); f.fake.fail = 'P'; CHECK(finish_frame(&f));
    return !f.scan.frame.position_available && !f.scan.frame.requested_angle_valid;
}
static bool parser_selects_both_modes(void)
{
    sonar_scan_config_t c = sonar_scan_config_default();
    CHECK(sonar_scan_command(&c, "mode continuous") == SCAN_CMD_UPDATED && c.mode == SCAN_CONTINUOUS);
    return sonar_scan_command(&c, "mode step") == SCAN_CMD_UPDATED && c.mode == SCAN_STEP_AND_STOP;
}
static bool parser_angles_and_velocity(void)
{
    sonar_scan_config_t c = sonar_scan_config_default();
    CHECK(sonar_scan_command(&c, "set step_deg -12.125") == SCAN_CMD_UPDATED && c.step_angle_mdeg == -12125);
    CHECK(sonar_scan_command(&c, "set start_deg 0.001") == SCAN_CMD_UPDATED && c.start_angle_mdeg == 1);
    return sonar_scan_command(&c, "set speed -3000.5") == SCAN_CMD_UPDATED && c.velocity == -30005000;
}
static bool parser_reference_and_period(void)
{
    sonar_scan_config_t c = sonar_scan_config_default();
    CHECK(sonar_scan_command(&c, "set steps_per_rev 3200") == SCAN_CMD_UPDATED && c.steps_per_revolution == 3200U);
    CHECK(sonar_scan_command(&c, "reference -400") == SCAN_CMD_UPDATED && c.reference_confirmed && c.reference_steps == -400);
    CHECK(sonar_scan_command(&c, "set interval_ms 5000") == SCAN_CMD_UPDATED && c.interval_ms == 5000U);
    CHECK(sonar_scan_command(&c, "unreference") == SCAN_CMD_UPDATED); return !c.reference_confirmed;
}
static bool parser_invalid_edits_preserve_plan(void)
{
    static const char *const bad[] = {"mode rotating", "set shots -1", "set speed 50000.1", "set step_deg 1.0001",
        "set interval_ms 999999999999999999", "set shots 3 extra", "set unknown 2", "set speed nan", "reference 2147483648"};
    sonar_scan_config_t c = sonar_scan_config_default(), previous = c;
    for (unsigned i = 0U; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        CHECK(sonar_scan_command(&c, bad[i]) == SCAN_CMD_INVALID);
        CHECK(memcmp(&c, &previous, sizeof(c)) == 0);
    }
    return true;
}
static bool parser_actions_do_not_edit(void)
{
    sonar_scan_config_t c = sonar_scan_config_default(), previous = c;
    CHECK(sonar_scan_command(&c, "show") == SCAN_CMD_SHOW);
    CHECK(sonar_scan_command(&c, "check") == SCAN_CMD_CHECK);
    CHECK(sonar_scan_command(&c, "simulate") == SCAN_CMD_SIMULATE);
    CHECK(sonar_scan_command(&c, "run") == SCAN_CMD_RUN);
    CHECK(sonar_scan_command(&c, "cancel") == SCAN_CMD_CANCEL);
    CHECK(sonar_scan_command(&c, "help") == SCAN_CMD_HELP);
    return memcmp(&c, &previous, sizeof(c)) == 0;
}
static bool null_inputs(void)
{
    sonar_scan_poll(NULL, 0U); sonar_scan_cancel(NULL); sonar_scan_event(NULL, NULL, 0U);
    return !sonar_scan_start(NULL, 0U) && !sonar_scan_frame(NULL, NULL) && !sonar_scan_release(NULL, 0U) &&
        sonar_scan_command(NULL, "show") == SCAN_CMD_INVALID;
}
static bool pending_error_never_triggers(void)
{
    fixture_t f; CHECK(setup(&f, SCAN_CONTINUOUS, 1U)); CHECK(sonar_scan_start(&f.scan, 0U));
    event(&f, SCAN_EVENT_MOTION_DONE, 1U); event(&f, SCAN_EVENT_ERROR, 3U);
    return f.scan.fault == SCAN_IO_ERROR && f.fake.count == 2U && f.fake.trace[1] == 'X';
}
static bool simulate_modes(void)
{
    for (unsigned mode = 0U; mode < 2U; ++mode) {
        sonar_scan_sim_t sim; sonar_scan_config_t c = sonar_scan_config_default();
        c.mode = (sonar_scan_mode_t)mode; c.steps_per_revolution = 3600U; c.reference_confirmed = true;
        CHECK(sonar_scan_sim_init(&sim, &c)); uint32_t prior = 0U;
        for (unsigned i = 0U; i < 50U && sim.scan.state != SCAN_COMPLETE; ++i) {
            sonar_scan_sim_step(&sim); sonar_measurement_t frame;
            if (sonar_scan_frame(&sim.scan, &frame)) {
                if (frame.index > 0U) { CHECK(frame.trigger_tick - prior >= c.interval_ms); }
                CHECK(frame.requested_angle_valid == (mode == 0U));
                CHECK(frame.position_available == (mode == 0U)); prior = frame.trigger_tick;
                CHECK(sonar_scan_sim_release(&sim));
            }
        }
        CHECK(sim.scan.state == SCAN_COMPLETE && sim.frames == c.shots && !sim.armed);
        CHECK(sim.moves == (mode == 0U ? c.shots : 1U));
    }
    return true;
}
static bool simulation_plan_is_copied(void)
{
    sonar_scan_sim_t sim; sonar_scan_config_t c = sonar_scan_config_default(); c.mode = SCAN_CONTINUOUS;
    CHECK(sonar_scan_sim_init(&sim, &c)); c.shots = 99U;
    return sim.scan.config.shots == 3U;
}
static bool simulation_cancel_releases_ownership(void)
{
    sonar_scan_sim_t sim; sonar_scan_config_t c = sonar_scan_config_default(); c.mode = SCAN_CONTINUOUS;
    CHECK(sonar_scan_sim_init(&sim, &c)); sonar_scan_sim_step(&sim); sonar_scan_sim_step(&sim);
    CHECK(sim.armed); sonar_scan_cancel(&sim.scan); sonar_scan_sim_step(&sim);
    return !sim.armed && sim.scan.fault == SCAN_CANCELLED && !sonar_scan_sim_release(&sim);
}
static bool simulation_held_frame_times_out(void)
{
    sonar_scan_sim_t sim; sonar_scan_config_t c = sonar_scan_config_default(); c.mode = SCAN_CONTINUOUS;
    c.frame_hold_timeout_ms = 2U;
    CHECK(sonar_scan_sim_init(&sim, &c));
    for (unsigned i = 0U; i < 5U; ++i) { sonar_scan_sim_step(&sim); }
    return sim.scan.fault == SCAN_DEADLINE && !sim.armed;
}
static bool simulation_long_intervals_are_bounded(void)
{
    sonar_scan_sim_t sim; sonar_scan_config_t c = sonar_scan_config_default(); c.mode = SCAN_CONTINUOUS;
    c.interval_ms = 2000000000U;
    CHECK(sonar_scan_sim_init(&sim, &c));
    for (unsigned i = 0U; i < 30U && sim.scan.state != SCAN_COMPLETE; ++i) {
        sonar_scan_sim_step(&sim);
        if (sim.scan.state == SCAN_FRAME_READY) { CHECK(sonar_scan_sim_release(&sim)); }
    }
    return sim.scan.state == SCAN_COMPLETE && sim.frames == 3U;
}
static bool parser_boundaries(void)
{
    sonar_scan_config_t c = sonar_scan_config_default(); char line[129];
    memset(line, 'a', sizeof(line)); line[128] = '\0';
    CHECK(sonar_scan_command(&c, line) == SCAN_CMD_INVALID);
    CHECK(sonar_scan_command(&c, "reference -2147483648") == SCAN_CMD_UPDATED && c.reference_steps == INT32_MIN);
    CHECK(sonar_scan_command(&c, "set interval_ms 4294967295") == SCAN_CMD_UPDATED && c.interval_ms == UINT32_MAX);
    CHECK(sonar_scan_command(&c, "set interval_ms 4294967296") == SCAN_CMD_INVALID);
    return sonar_scan_command(&c, "  mode\tcontinuous\r\n") == SCAN_CMD_UPDATED;
}
sonar_test_result_t sonar_scan_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"scan defaults require calibration", defaults_require_calibration},
        {"scan angle conversion and rounding", angle_conversion_and_rounding},
        {"scan target overflow rejected", target_overflow_rejected},
        {"scan configuration bounds", invalid_limits_and_time_preserve_output},
        {"scan arm before trigger", acquisition_order},
        {"scan waits for motion and settling", settling_is_respected},
        {"scan needs TX and RX completion", both_completions_required},
        {"scan either completion order", reverse_completion_order},
        {"scan holds frame ownership", held_frame_prevents_rearm},
        {"scan continuous cadence", continuous_cadence_and_single_motor_start},
        {"scan stepped angles", step_mode_moves_each_angle},
        {"scan waits for final stop", final_stop_requires_event},
        {"scan stale generations", stale_events_ignored},
        {"scan duplicate RX event", duplicate_rx_keeps_first_timestamp},
        {"scan simultaneous error", error_wins_over_completion},
        {"scan exact capture length", exact_capture_length},
        {"scan overflow rejection", overflow_is_not_a_valid_frame},
        {"scan unknown integrity rejection", missing_integrity_is_explicit},
        {"scan failed move/arm/trigger", failed_callbacks_abort},
        {"scan failed release/stop", failed_release_or_stop_aborts},
        {"scan inclusive motion deadline", inclusive_motion_deadline},
        {"scan missing TX deadline", missing_tx_times_out},
        {"scan held-frame deadline", held_frame_deadline},
        {"scan final stop deadline", missing_stop_times_out},
        {"scan future event timestamp", future_event_rejected},
        {"scan cancellation uncertainty", cancellation_retains_uncertainty},
        {"scan tick wrap", timing_wrap},
        {"scan unavailable position labelled", unavailable_position_is_labelled},
        {"scan parser rotation modes", parser_selects_both_modes},
        {"scan parser decimal angle/speed", parser_angles_and_velocity},
        {"scan parser calibration/period", parser_reference_and_period},
        {"scan parser rejects invalid edits", parser_invalid_edits_preserve_plan},
        {"scan parser actions", parser_actions_do_not_edit},
        {"scan null arguments", null_inputs},
        {"scan error cannot start a transfer", pending_error_never_triggers},
        {"scan simulation both modes and cadence", simulate_modes},
        {"scan simulation freezes configuration", simulation_plan_is_copied},
        {"scan simulation cancel releases buffer", simulation_cancel_releases_ownership},
        {"scan simulation held-frame timeout", simulation_held_frame_times_out},
        {"scan simulation long interval bounded work", simulation_long_intervals_are_bounded},
        {"scan parser input boundaries", parser_boundaries}
    };
    sonar_test_result_t result = {0U,0U};
    for (unsigned i = 0U; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
