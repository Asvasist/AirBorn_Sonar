#include "sonar_mic_selftest.h"
#include "sonar_mic.h"
#include <stddef.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { return false; } } while (0)

typedef struct {
    char trace[20];
    uint32_t calls;
    char fail;
    uint32_t cache_bytes;
    uint32_t dma_bytes;
    uint32_t token;
    uint8_t *address;
} fake_t;

static bool call(fake_t *fake, char operation)
{
    if (fake->calls < sizeof(fake->trace)) { fake->trace[fake->calls++] = operation; }
    return fake->fail != operation;
}
static bool prepare(void *context, uint8_t *buffer, uint32_t span)
{
    fake_t *fake = context;
    fake->cache_bytes = span; fake->address = buffer;
    return call(fake, 'P');
}
static bool arm(void *context, uint8_t *buffer, uint32_t bytes, uint32_t token)
{
    fake_t *fake = context;
    fake->dma_bytes = bytes; fake->address = buffer; fake->token = token;
    return call(fake, 'A');
}
static bool trigger(void *context) { return call(context, 'T'); }
static bool finish(void *context, uint8_t *buffer, uint32_t span)
{
    fake_t *fake = context;
    fake->cache_bytes = span; fake->address = buffer;
    return call(fake, 'F');
}
static bool abort_transfer(void *context) { return call(context, 'R'); }

typedef struct {
    _Alignas(SONAR_MIC_CACHE_LINE) uint8_t buffer[96];
    fake_t fake;
    sonar_mic_t capture;
} fixture_t;

static bool setup(fixture_t *fixture)
{
    const sonar_mic_config_t config = {68U, 500U, 2400000U};
    sonar_mic_io_t io = {&fixture->fake, prepare, arm, trigger, finish, abort_transfer};
    memset(fixture, 0, sizeof(*fixture));
    return sonar_mic_init(&fixture->capture, &config, &io, fixture->buffer,
                          (uint32_t)sizeof(fixture->buffer));
}
static sonar_mic_event_t done(const fixture_t *fixture)
{
    const sonar_mic_event_t event = {fixture->capture.generation, SONAR_MIC_IRQ_DONE,
                                    68U, 100U, false};
    return event;
}
static bool start_order_and_cache_span(void)
{
    fixture_t f;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U));
    CHECK(memcmp(f.fake.trace, "PAT", 3U) == 0 && f.fake.calls == 3U);
    CHECK(f.fake.cache_bytes == 96U && f.fake.dma_bytes == 68U);
    return f.capture.dma_owned && f.capture.state == MIC_CAPTURING && f.fake.token == 1U;
}
static bool buffer_hidden_until_completion(void)
{
    fixture_t f; sonar_mic_frame_t frame;
    CHECK(setup(&f)); CHECK(!sonar_mic_frame(&f.capture, &frame));
    CHECK(sonar_mic_start(&f.capture, 0U));
    return !sonar_mic_frame(&f.capture, &frame) && !sonar_mic_release(&f.capture);
}
static bool completion_returns_cpu_ownership(void)
{
    fixture_t f; sonar_mic_frame_t frame; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    sonar_mic_event(&f.capture, &event, 101U);
    CHECK(sonar_mic_frame(&f.capture, &frame));
    CHECK(f.capture.state == MIC_READY && !f.capture.dma_owned);
    CHECK(memcmp(f.fake.trace, "PATF", 4U) == 0);
    CHECK(frame.data == f.buffer && frame.bytes == 68U && frame.generation == 1U);
    return frame.request_tick == 0U && frame.completion_tick == 100U &&
           frame.pdm_hz == 2400000U && !frame.overflow_checked;
}
static bool held_frame_prevents_reuse(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U));
    CHECK(!sonar_mic_start(&f.capture, 1U)); event = done(&f);
    sonar_mic_event(&f.capture, &event, 100U);
    CHECK(!sonar_mic_start(&f.capture, 200U));
    CHECK(sonar_mic_release(&f.capture));
    CHECK(sonar_mic_start(&f.capture, 200U));
    return f.capture.generation == 2U && f.fake.calls == 7U;
}
static bool stale_and_duplicate_events(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    event.generation = 0U; sonar_mic_event(&f.capture, &event, 100U);
    CHECK(f.capture.state == MIC_CAPTURING && f.fake.calls == 3U);
    event.generation = 1U; sonar_mic_event(&f.capture, &event, 100U);
    sonar_mic_event(&f.capture, &event, 101U);
    CHECK(f.fake.calls == 4U);
    CHECK(sonar_mic_release(&f.capture)); CHECK(sonar_mic_start(&f.capture, 200U));
    sonar_mic_event(&f.capture, &event, 210U);
    return f.capture.state == MIC_CAPTURING;
}
static bool dma_error_wins_over_done(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    event.flags |= SONAR_MIC_IRQ_ERROR; sonar_mic_event(&f.capture, &event, 100U);
    return f.capture.fault == MIC_DMA_ERROR && !f.capture.dma_owned &&
           f.fake.trace[3] == 'R';
}
static bool overflow_rejects_frame(void)
{
    fixture_t f; sonar_mic_event_t event; sonar_mic_frame_t frame;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    event.flags |= SONAR_MIC_IRQ_OVERFLOW; sonar_mic_event(&f.capture, &event, 100U);
    return f.capture.fault == MIC_OVERFLOW && !sonar_mic_frame(&f.capture, &frame);
}
static bool observable_overflow_metadata(void)
{
    fixture_t f; sonar_mic_event_t event; sonar_mic_frame_t frame;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    event.overflow_observable = true; sonar_mic_event(&f.capture, &event, 100U);
    return sonar_mic_frame(&f.capture, &frame) && frame.overflow_checked;
}
static bool invalid_lengths(void)
{
    const uint32_t lengths[] = {0U, 64U, 67U, 72U, UINT32_MAX};
    size_t i;
    for (i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        fixture_t f; sonar_mic_event_t event;
        CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
        event.bytes = lengths[i]; sonar_mic_event(&f.capture, &event, 100U);
        CHECK(f.capture.fault == MIC_BAD_LENGTH && f.fake.trace[3] == 'R');
    }
    return true;
}
static bool timeout_boundary_and_latch(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    sonar_mic_poll(&f.capture, 499U); CHECK(f.capture.state == MIC_CAPTURING);
    sonar_mic_poll(&f.capture, 500U); CHECK(f.capture.fault == MIC_TIMEOUT);
    sonar_mic_event(&f.capture, &event, 501U); sonar_mic_cancel(&f.capture);
    CHECK(f.fake.calls == 4U && f.capture.fault == MIC_TIMEOUT);
    return !sonar_mic_start(&f.capture, 600U) && !sonar_mic_release(&f.capture);
}
static bool late_and_future_events(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    sonar_mic_event(&f.capture, &event, 500U); CHECK(f.capture.fault == MIC_TIMEOUT);
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    event.tick = 102U; sonar_mic_event(&f.capture, &event, 100U);
    return f.capture.fault == MIC_TIMEOUT;
}
static bool wraparound_deadline(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, UINT32_MAX - 49U));
    event = done(&f); sonar_mic_event(&f.capture, &event, 101U);
    CHECK(f.capture.state == MIC_READY);
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, UINT32_MAX - 49U));
    sonar_mic_poll(&f.capture, 450U);
    return f.capture.fault == MIC_TIMEOUT;
}
static bool reset_failure_quarantines_buffer(void)
{
    fixture_t f; sonar_mic_frame_t frame;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); f.fake.fail = 'R';
    sonar_mic_poll(&f.capture, 500U);
    return f.capture.fault == MIC_TIMEOUT && f.capture.dma_owned &&
           !sonar_mic_frame(&f.capture, &frame) && !sonar_mic_release(&f.capture);
}
static bool cancelled_capture_is_terminal(void)
{
    fixture_t f;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U));
    sonar_mic_cancel(&f.capture); sonar_mic_cancel(&f.capture);
    return f.capture.fault == MIC_CANCELLED && f.fake.calls == 4U && !f.capture.dma_owned;
}
static bool preparation_failure_does_not_arm(void)
{
    fixture_t f;
    CHECK(setup(&f)); f.fake.fail = 'P'; CHECK(!sonar_mic_start(&f.capture, 0U));
    return f.capture.fault == MIC_PREPARE_FAILED && f.fake.calls == 1U && !f.capture.dma_owned;
}
static bool arm_failure_resets_before_return(void)
{
    fixture_t f;
    CHECK(setup(&f)); f.fake.fail = 'A'; CHECK(!sonar_mic_start(&f.capture, 0U));
    return f.capture.fault == MIC_ARM_FAILED && memcmp(f.fake.trace, "PAR", 3U) == 0;
}
static bool trigger_failure_resets(void)
{
    fixture_t f;
    CHECK(setup(&f)); f.fake.fail = 'T'; CHECK(!sonar_mic_start(&f.capture, 0U));
    return f.capture.fault == MIC_TRIGGER_FAILED && memcmp(f.fake.trace, "PATR", 4U) == 0;
}
static bool finish_failure_hides_frame(void)
{
    fixture_t f; sonar_mic_event_t event; sonar_mic_frame_t frame;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); f.fake.fail = 'F'; event = done(&f);
    sonar_mic_event(&f.capture, &event, 100U);
    return f.capture.fault == MIC_FINISH_FAILED && !sonar_mic_frame(&f.capture, &frame) &&
           memcmp(f.fake.trace, "PATFR", 5U) == 0;
}
static bool bad_buffer_or_config(void)
{
    fixture_t f; sonar_mic_config_t config; sonar_mic_io_t io;
    CHECK(setup(&f)); config = f.capture.config; io = f.capture.io;
    CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer + 1U, 95U));
    CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 68U));
    config.bytes = 0U; CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U));
    config.bytes = 67U; CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U));
    config.bytes = UINT32_MAX; CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U));
    config.bytes = 68U; config.timeout_ticks = 0U;
    CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U));
    config.timeout_ticks = UINT32_MAX;
    CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U));
    config.timeout_ticks = 500U; config.pdm_hz = 0U;
    return !sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U);
}
static bool null_inputs_and_missing_callbacks(void)
{
    fixture_t f; sonar_mic_io_t io; sonar_mic_config_t config;
    CHECK(setup(&f)); io = f.capture.io; config = f.capture.config;
    CHECK(!sonar_mic_init(NULL, &config, &io, f.buffer, 96U));
    CHECK(!sonar_mic_init(&f.capture, NULL, &io, f.buffer, 96U));
    CHECK(!sonar_mic_init(&f.capture, &config, NULL, f.buffer, 96U));
    CHECK(!sonar_mic_init(&f.capture, &config, &io, NULL, 96U));
    io.abort = NULL; CHECK(!sonar_mic_init(&f.capture, &config, &io, f.buffer, 96U));
    CHECK(!sonar_mic_start(NULL, 0U)); CHECK(!sonar_mic_release(NULL));
    CHECK(!sonar_mic_frame(NULL, NULL));
    sonar_mic_event(NULL, NULL, 0U); sonar_mic_poll(NULL, 0U); sonar_mic_cancel(NULL);
    return true;
}
static bool empty_interrupt_does_not_complete(void)
{
    fixture_t f; sonar_mic_event_t event;
    CHECK(setup(&f)); CHECK(sonar_mic_start(&f.capture, 0U)); event = done(&f);
    event.flags = 0U; sonar_mic_event(&f.capture, &event, 100U);
    sonar_mic_event(&f.capture, NULL, 100U);
    return f.capture.state == MIC_CAPTURING && f.fake.calls == 3U;
}
static bool generation_wrap(void)
{
    fixture_t f;
    CHECK(setup(&f)); f.capture.generation = UINT32_MAX;
    CHECK(sonar_mic_start(&f.capture, 0U));
    return f.capture.generation == 0U && f.fake.token == 0U;
}

sonar_test_result_t sonar_mic_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"MIC cache/arm/trigger ordering", start_order_and_cache_span},
        {"MIC buffer ownership before completion", buffer_hidden_until_completion},
        {"MIC completed frame metadata", completion_returns_cpu_ownership},
        {"MIC held buffer and release", held_frame_prevents_reuse},
        {"MIC stale/duplicate IRQ events", stale_and_duplicate_events},
        {"MIC simultaneous error/completion", dma_error_wins_over_done},
        {"MIC overflow rejection", overflow_rejects_frame},
        {"MIC overflow visibility metadata", observable_overflow_metadata},
        {"MIC exact frame length", invalid_lengths},
        {"MIC timeout and first-fault latch", timeout_boundary_and_latch},
        {"MIC late/future completion", late_and_future_events},
        {"MIC tick rollover", wraparound_deadline},
        {"MIC failed reset quarantine", reset_failure_quarantines_buffer},
        {"MIC cancellation", cancelled_capture_is_terminal},
        {"MIC cache preparation failure", preparation_failure_does_not_arm},
        {"MIC DMA arm failure", arm_failure_resets_before_return},
        {"MIC trigger failure", trigger_failure_resets},
        {"MIC completion/cache failure", finish_failure_hides_frame},
        {"MIC invalid buffers/configuration", bad_buffer_or_config},
        {"MIC null inputs/callbacks", null_inputs_and_missing_callbacks},
        {"MIC empty interrupt", empty_interrupt_does_not_complete},
        {"MIC generation rollover", generation_wrap}
    };
    sonar_test_result_t result = {0U, 0U}; size_t i;
    for (i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
