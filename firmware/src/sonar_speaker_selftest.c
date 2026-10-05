#include "sonar_speaker.h"
#include "sonar_component_selftest.h"
#include <stddef.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { return false; } } while (0)
typedef struct {
    uint8_t bytes[10][2], address;
    unsigned writes, aborts, enables, mutes;
    uint32_t token;
    bool fail_write, fail_abort, fail_mute, fail_enable;
} fake_t;
typedef struct { sonar_speaker_t speaker; fake_t fake; } fixture_t;
static bool write_bytes(void *context, uint8_t address, const uint8_t bytes[2], uint32_t token)
{
    fake_t *f = context;
    if (f->writes < 10U) { memcpy(f->bytes[f->writes], bytes, 2U); }
    ++f->writes; f->address = address; f->token = token; return !f->fail_write;
}
static bool abort_bus(void *context)
{
    fake_t *f = context; ++f->aborts; return !f->fail_abort;
}
static bool enable(void *context, bool enabled)
{
    fake_t *f = context;
    if (enabled) { ++f->enables; return !f->fail_enable; }
    ++f->mutes; return !f->fail_mute;
}
static bool setup(fixture_t *f)
{
    const sonar_speaker_config_t config = {10U, 80U, 100U, 127U};
    const sonar_speaker_io_t io = {&f->fake, write_bytes, abort_bus, enable};
    memset(f, 0, sizeof(*f)); return sonar_speaker_init(&f->speaker, &config, &io);
}
static bool first_write(fixture_t *f, uint32_t now)
{
    CHECK(sonar_speaker_begin(&f->speaker, now));
    sonar_speaker_poll(&f->speaker, now + 10U);
    return f->fake.writes == 1U;
}
static bool finish(fixture_t *f)
{
    CHECK(first_write(f, 0U));
    for (unsigned i = 0U; i < 8U; ++i) {
        sonar_speaker_event(&f->speaker, f->fake.token, true, 11U + i);
    }
    CHECK(f->fake.writes == 8U && f->speaker.phase == SPEAKER_WAIT_VMID);
    sonar_speaker_poll(&f->speaker, 98U);
    sonar_speaker_event(&f->speaker, f->fake.token, true, 99U);
    sonar_speaker_event(&f->speaker, f->fake.token, true, 100U);
    return f->speaker.state == SPEAKER_READY;
}
static bool defaults_round_delays_up(void)
{
    sonar_speaker_config_t c = {0U, 0U, 0U, 0U};
    CHECK(sonar_speaker_default_config(100U, &c));
    CHECK(c.powerup_ticks == 1U && c.vmid_ticks == 8U && c.write_timeout_ticks == 10U);
    CHECK(c.volume == 127U); CHECK(!sonar_speaker_default_config(0U, &c));
    return !sonar_speaker_default_config(100U, NULL) && c.powerup_ticks == 1U;
}
static bool codec_encoding(void)
{
    uint8_t bytes[2] = {0U, 0U};
    CHECK(sonar_speaker_encode(2U, 0x17fU, bytes));
    CHECK(bytes[0] == 5U && bytes[1] == 0x7fU);
    CHECK(sonar_speaker_encode(127U, 511U, bytes));
    CHECK(bytes[0] == 255U && bytes[1] == 255U);
    CHECK(!sonar_speaker_encode(128U, 0U, bytes));
    CHECK(!sonar_speaker_encode(0U, 512U, bytes));
    return bytes[0] == 255U && bytes[1] == 255U && !sonar_speaker_encode(0U, 0U, NULL);
}
static bool validates_configuration(void)
{
    fixture_t f; sonar_speaker_config_t c; sonar_speaker_io_t io;
    CHECK(setup(&f)); c = f.speaker.config; io = f.speaker.io;
    CHECK(!sonar_speaker_init(NULL, &c, &io));
    CHECK(!sonar_speaker_init(&f.speaker, NULL, &io));
    CHECK(!sonar_speaker_init(&f.speaker, &c, NULL));
    c.volume = 128U; CHECK(!sonar_speaker_init(&f.speaker, &c, &io)); c.volume = 127U;
    c.powerup_ticks = 0U; CHECK(!sonar_speaker_init(&f.speaker, &c, &io)); c.powerup_ticks = 10U;
    c.vmid_ticks = UINT32_C(0x80000000); CHECK(!sonar_speaker_init(&f.speaker, &c, &io)); c.vmid_ticks = 80U;
    c.write_timeout_ticks = 0U; CHECK(!sonar_speaker_init(&f.speaker, &c, &io));
    return f.speaker.state == SPEAKER_OFF && f.fake.writes == 0U;
}
static bool requires_all_callbacks(void)
{
    fixture_t f; sonar_speaker_io_t io; CHECK(setup(&f)); io = f.speaker.io;
    io.write = NULL; CHECK(!sonar_speaker_init(&f.speaker, &f.speaker.config, &io));
    io = f.speaker.io; io.abort = NULL; CHECK(!sonar_speaker_init(&f.speaker, &f.speaker.config, &io));
    io = f.speaker.io; io.enable = NULL; return !sonar_speaker_init(&f.speaker, &f.speaker.config, &io);
}
static bool mutes_before_power_delay(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_speaker_begin(&f.speaker, 20U));
    CHECK(f.fake.mutes == 1U && f.fake.writes == 0U && f.speaker.mute_confirmed);
    sonar_speaker_poll(&f.speaker, 29U); CHECK(f.fake.writes == 0U);
    sonar_speaker_poll(&f.speaker, 30U); return f.fake.writes == 1U && f.speaker.bus_owned;
}
static bool exact_supplied_register_sequence(void)
{
    static const uint8_t expected[10][2] = {
        {0x1eU, 0x00U}, {0x0cU, 0x77U}, {0x05U, 0x7fU}, {0x07U, 0x7fU},
        {0x08U, 0x10U}, {0x0aU, 0x00U}, {0x0eU, 0x02U}, {0x10U, 0x00U},
        {0x12U, 0x01U}, {0x0cU, 0x67U}
    };
    fixture_t f; CHECK(setup(&f)); CHECK(finish(&f));
    return memcmp(expected, f.fake.bytes, sizeof(expected)) == 0 && f.fake.address == 0x1aU &&
        f.fake.writes == 10U && !f.speaker.output_enabled && f.fake.enables == 0U && !f.speaker.bus_owned;
}
static bool vmid_delay_after_sampling_register(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, 0U));
    for (unsigned i = 0U; i < 8U; ++i) { sonar_speaker_event(&f.speaker, f.fake.token, true, 11U + i); }
    sonar_speaker_poll(&f.speaker, 97U); CHECK(f.fake.writes == 8U);
    sonar_speaker_poll(&f.speaker, 98U); return f.fake.writes == 9U;
}
static bool volume_is_local_to_two_registers(void)
{
    fixture_t f; CHECK(setup(&f)); f.speaker.config.volume = 42U; CHECK(finish(&f));
    return f.fake.bytes[2][0] == 5U && f.fake.bytes[2][1] == 42U &&
        f.fake.bytes[3][0] == 7U && f.fake.bytes[3][1] == 42U;
}
static bool output_requires_explicit_enable(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(!sonar_speaker_enable(&f.speaker, true));
    CHECK(finish(&f)); CHECK(sonar_speaker_enable(&f.speaker, true));
    CHECK(f.speaker.output_enabled && !f.speaker.mute_confirmed && f.fake.enables == 1U);
    CHECK(sonar_speaker_enable(&f.speaker, true)); CHECK(f.fake.enables == 1U);
    CHECK(sonar_speaker_enable(&f.speaker, false));
    return f.speaker.mute_confirmed && !f.speaker.output_enabled;
}
static bool duplicate_and_stale_ack_ignored(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, 0U));
    sonar_speaker_event(&f.speaker, 0U, false, 11U); CHECK(f.fake.writes == 1U);
    sonar_speaker_event(&f.speaker, 1U, true, 11U); CHECK(f.fake.writes == 2U);
    sonar_speaker_event(&f.speaker, 1U, true, 12U);
    return f.fake.writes == 2U && f.speaker.token == 2U;
}
static bool failed_ack_at_each_register(void)
{
    for (unsigned index = 0U; index < 10U; ++index) {
        fixture_t f; uint32_t now = 11U; CHECK(setup(&f)); CHECK(first_write(&f, 0U));
        for (unsigned i = 0U; i < index; ++i) {
            sonar_speaker_event(&f.speaker, f.fake.token, true, now++);
            if (f.speaker.phase == SPEAKER_WAIT_VMID) { now += 80U; sonar_speaker_poll(&f.speaker, now); }
        }
        sonar_speaker_event(&f.speaker, f.fake.token, false, now);
        CHECK(f.speaker.state == SPEAKER_FAULT && f.speaker.fault == SPEAKER_BUS_ERROR);
        CHECK(f.fake.writes == index + 1U && f.fake.aborts == 1U && f.speaker.mute_confirmed);
    }
    return true;
}
static bool write_failure_aborts_partial_transaction(void)
{
    fixture_t f; CHECK(setup(&f)); f.fake.fail_write = true; CHECK(first_write(&f, 0U));
    return f.speaker.fault == SPEAKER_WRITE_FAILED && f.fake.aborts == 1U && !f.speaker.bus_owned;
}
static bool failed_abort_quarantines_bus(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, 0U)); f.fake.fail_abort = true;
    sonar_speaker_cancel(&f.speaker); CHECK(f.speaker.bus_owned);
    sonar_speaker_event(&f.speaker, 1U, true, 11U); sonar_speaker_poll(&f.speaker, 200U);
    sonar_speaker_cancel(&f.speaker);
    return !sonar_speaker_begin(&f.speaker, 201U) && f.fake.aborts == 1U && f.fake.writes == 1U;
}
static bool mute_failure_prevents_writes(void)
{
    fixture_t f; CHECK(setup(&f)); f.fake.fail_mute = true;
    CHECK(!sonar_speaker_begin(&f.speaker, 0U));
    return f.speaker.fault == SPEAKER_MUTE_FAILED && !f.speaker.mute_confirmed && f.fake.writes == 0U;
}
static bool enable_failure_remutes_and_latches(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(finish(&f)); f.fake.fail_enable = true;
    CHECK(!sonar_speaker_enable(&f.speaker, true));
    return f.speaker.state == SPEAKER_FAULT && f.speaker.mute_confirmed && f.fake.mutes == 2U;
}
static bool timeout_and_late_ack(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, 0U));
    sonar_speaker_poll(&f.speaker, 109U); CHECK(f.speaker.state == SPEAKER_CONFIGURING);
    sonar_speaker_event(&f.speaker, 1U, true, 110U);
    return f.speaker.fault == SPEAKER_TIMEOUT && f.fake.writes == 1U;
}
static bool missing_ack_times_out(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, 0U)); sonar_speaker_poll(&f.speaker, 110U);
    return f.speaker.fault == SPEAKER_TIMEOUT && f.fake.aborts == 1U;
}
static bool waits_across_tick_wrap(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, UINT32_MAX - 4U));
    sonar_speaker_poll(&f.speaker, 104U); CHECK(f.speaker.state == SPEAKER_CONFIGURING);
    sonar_speaker_poll(&f.speaker, 105U); return f.speaker.fault == SPEAKER_TIMEOUT;
}
static bool cancellation_during_delay(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(sonar_speaker_begin(&f.speaker, 0U));
    sonar_speaker_cancel(&f.speaker); sonar_speaker_poll(&f.speaker, 1000U);
    return f.speaker.fault == SPEAKER_CANCELLED && f.fake.writes == 0U && f.fake.aborts == 0U;
}
static bool cancellation_after_enable(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(finish(&f)); CHECK(sonar_speaker_enable(&f.speaker, true));
    sonar_speaker_cancel(&f.speaker);
    return f.speaker.fault == SPEAKER_CANCELLED && f.speaker.mute_confirmed && !f.speaker.output_enabled;
}
static bool refuses_reconfiguration(void)
{
    fixture_t f; CHECK(setup(&f)); CHECK(first_write(&f, 0U));
    CHECK(!sonar_speaker_begin(&f.speaker, 0U)); CHECK(f.fake.mutes == 1U);
    CHECK(setup(&f)); CHECK(finish(&f)); return !sonar_speaker_begin(&f.speaker, 101U);
}
static bool token_wrap_skips_zero(void)
{
    fixture_t f; CHECK(setup(&f)); f.speaker.token = UINT32_MAX;
    CHECK(first_write(&f, 0U)); return f.fake.token == 1U;
}
static bool null_calls_are_rejected(void)
{
    sonar_speaker_poll(NULL, 0U); sonar_speaker_event(NULL, 1U, true, 0U); sonar_speaker_cancel(NULL);
    return !sonar_speaker_begin(NULL, 0U) && !sonar_speaker_enable(NULL, true);
}
sonar_test_result_t sonar_speaker_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"speaker default delays", defaults_round_delays_up},
        {"speaker codec byte encoding", codec_encoding},
        {"speaker validates configuration", validates_configuration},
        {"speaker requires callbacks", requires_all_callbacks},
        {"speaker mutes before power delay", mutes_before_power_delay},
        {"speaker supplied register sequence", exact_supplied_register_sequence},
        {"speaker VMID delay", vmid_delay_after_sampling_register},
        {"speaker volume parameter", volume_is_local_to_two_registers},
        {"speaker explicit output enable", output_requires_explicit_enable},
        {"speaker stale and duplicate ACK", duplicate_and_stale_ack_ignored},
        {"speaker NACK at each register", failed_ack_at_each_register},
        {"speaker partial write failure", write_failure_aborts_partial_transaction},
        {"speaker failed abort quarantines bus", failed_abort_quarantines_bus},
        {"speaker mute failure blocks writes", mute_failure_prevents_writes},
        {"speaker enable failure remutes", enable_failure_remutes_and_latches},
        {"speaker late ACK times out", timeout_and_late_ack},
        {"speaker missing ACK times out", missing_ack_times_out},
        {"speaker tick wrap", waits_across_tick_wrap},
        {"speaker cancel during delay", cancellation_during_delay},
        {"speaker cancel after enable", cancellation_after_enable},
        {"speaker reconfiguration rejected", refuses_reconfiguration},
        {"speaker token wrap", token_wrap_skips_zero},
        {"speaker null calls", null_calls_are_rejected}
    };
    sonar_test_result_t result = {0U, 0U};
    for (unsigned i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
