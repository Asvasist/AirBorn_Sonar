#include "sonar_motor.h"
#include "sonar_component_selftest.h"
#include <stddef.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { return false; } } while (0)
typedef struct { unsigned starts, stops; uint32_t token; bool fail_start, fail_stop; } fake_t;
typedef struct { sonar_motor_t motor; fake_t fake; } fixture_t;
static bool start(void *context, uint32_t token)
{
    fake_t *f = context; ++f->starts; f->token = token; return !f->fail_start;
}
static bool stop(void *context)
{
    fake_t *f = context; ++f->stops; return !f->fail_stop;
}
static bool setup(fixture_t *f)
{
    sonar_motor_io_t io = {&f->fake, start, stop};
    memset(f, 0, sizeof(*f));
    return sonar_motor_init(&f->motor, &io, 100U);
}
static bool validates_contract(void)
{
    fixture_t f; sonar_motor_io_t io;
    CHECK(setup(&f)); io = f.motor.io;
    CHECK(!sonar_motor_init(NULL, &io, 100U));
    CHECK(!sonar_motor_init(&f.motor, NULL, 100U));
    CHECK(!sonar_motor_init(&f.motor, &io, 0U));
    CHECK(!sonar_motor_init(&f.motor, &io, UINT32_C(0x80000000)));
    io.stop = NULL; CHECK(!sonar_motor_init(&f.motor, &io, 100U));
    io = f.motor.io; io.start = NULL;
    return !sonar_motor_init(&f.motor, &io, 100U) && f.fake.starts == 0U && f.fake.stops == 0U;
}
static bool starts_once(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 10U));
    CHECK(!sonar_motor_start(&f.motor, 11U));
    return f.fake.starts == 1U && f.fake.token == 1U && !f.motor.stop_confirmed;
}
static bool completion_requires_observation(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 10U));
    sonar_motor_poll(&f.motor, 109U);
    CHECK(f.motor.state == MOTOR_RUNNING);
    sonar_motor_event(&f.motor, 1U, SONAR_MOTOR_DONE, 109U);
    return f.motor.state == MOTOR_DONE && f.motor.stop_confirmed && f.fake.stops == 0U;
}
static bool stale_event_ignored(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 0U));
    sonar_motor_event(&f.motor, 0U, SONAR_MOTOR_ERROR, 1U);
    sonar_motor_event(&f.motor, 2U, SONAR_MOTOR_DONE, 2U);
    return f.motor.state == MOTOR_RUNNING && f.fake.stops == 0U;
}
static bool unknown_event_ignored(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 0U));
    sonar_motor_event(&f.motor, 1U, 8U, 1U);
    return f.motor.state == MOTOR_RUNNING;
}
static bool error_wins_over_done(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 0U));
    sonar_motor_event(&f.motor, 1U, SONAR_MOTOR_DONE | SONAR_MOTOR_ERROR, 1U);
    return f.motor.state == MOTOR_FAULT && f.motor.fault == MOTOR_DRIVER_ERROR && f.fake.stops == 1U;
}
static bool timeout_is_inclusive(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 10U));
    sonar_motor_poll(&f.motor, 110U);
    return f.motor.fault == MOTOR_TIMEOUT && f.motor.stop_confirmed && f.fake.stops == 1U;
}
static bool late_done_cannot_hide_timeout(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 10U));
    sonar_motor_event(&f.motor, 1U, SONAR_MOTOR_DONE, 110U);
    return f.motor.state == MOTOR_FAULT && f.motor.fault == MOTOR_TIMEOUT;
}
static bool tick_wrap(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, UINT32_MAX - 49U));
    sonar_motor_poll(&f.motor, 49U); CHECK(f.motor.state == MOTOR_RUNNING);
    sonar_motor_poll(&f.motor, 50U); return f.motor.fault == MOTOR_TIMEOUT;
}
static bool failed_start_stops_partial_motion(void)
{
    fixture_t f; CHECK(setup(&f)); f.fake.fail_start = true;
    CHECK(!sonar_motor_start(&f.motor, 0U));
    return f.motor.fault == MOTOR_START_FAILED && f.fake.stops == 1U && f.motor.stop_confirmed;
}
static bool failed_stop_latches_unknown_motion(void)
{
    fixture_t f; CHECK(setup(&f)); f.fake.fail_stop = true;
    CHECK(sonar_motor_start(&f.motor, 0U)); sonar_motor_cancel(&f.motor);
    CHECK(!f.motor.stop_confirmed && f.motor.fault == MOTOR_CANCELLED);
    CHECK(!sonar_motor_start(&f.motor, 1U) && !sonar_motor_release(&f.motor));
    sonar_motor_cancel(&f.motor); sonar_motor_poll(&f.motor, 200U);
    return f.fake.stops == 1U && f.fake.starts == 1U;
}
static bool fault_cannot_be_cleared_by_done(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 0U));
    sonar_motor_cancel(&f.motor); sonar_motor_event(&f.motor, 1U, SONAR_MOTOR_DONE, 1U);
    return f.motor.state == MOTOR_FAULT && f.motor.fault == MOTOR_CANCELLED;
}
static bool held_result_blocks_next_motion(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_motor_start(&f.motor, 0U));
    sonar_motor_event(&f.motor, 1U, SONAR_MOTOR_DONE, 1U);
    CHECK(!sonar_motor_start(&f.motor, 2U)); CHECK(sonar_motor_release(&f.motor));
    CHECK(sonar_motor_start(&f.motor, 2U));
    sonar_motor_event(&f.motor, 1U, SONAR_MOTOR_DONE, 3U);
    return f.motor.state == MOTOR_RUNNING && f.motor.generation == 2U;
}
static bool generation_wrap_skips_zero(void)
{
    fixture_t f; CHECK(setup(&f)); f.motor.generation = UINT32_MAX;
    CHECK(sonar_motor_start(&f.motor, 0U)); return f.fake.token == 1U;
}
static bool idle_cancel_has_no_side_effect(void)
{
    fixture_t f; CHECK(setup(&f)); sonar_motor_cancel(&f.motor);
    CHECK(!sonar_motor_release(&f.motor)); return f.motor.state == MOTOR_IDLE && f.fake.stops == 0U;
}
static bool null_calls_are_rejected(void)
{
    sonar_motor_poll(NULL, 0U); sonar_motor_cancel(NULL); sonar_motor_event(NULL, 1U, 1U, 0U);
    return !sonar_motor_start(NULL, 0U) && !sonar_motor_release(NULL);
}
sonar_test_result_t sonar_motor_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"motor validates driver contract", validates_contract},
        {"motor starts only once", starts_once},
        {"motor completion needs observation", completion_requires_observation},
        {"motor ignores stale events", stale_event_ignored},
        {"motor ignores unknown events", unknown_event_ignored},
        {"motor error wins over done", error_wins_over_done},
        {"motor inclusive timeout", timeout_is_inclusive},
        {"motor late done times out", late_done_cannot_hide_timeout},
        {"motor tick wrap", tick_wrap},
        {"motor stops after partial start", failed_start_stops_partial_motion},
        {"motor failed stop latches unknown motion", failed_stop_latches_unknown_motion},
        {"motor fault remains latched", fault_cannot_be_cleared_by_done},
        {"motor holds completed result", held_result_blocks_next_motion},
        {"motor generation wrap", generation_wrap_skips_zero},
        {"motor idle cancel", idle_cancel_has_no_side_effect},
        {"motor null calls", null_calls_are_rejected}
    };
    sonar_test_result_t result = {0U, 0U};
    for (unsigned i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
