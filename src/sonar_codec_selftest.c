#include "sonar_codec_bus.h"
#include "sonar_component_selftest.h"
#include <stddef.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { return false; } } while (0)
typedef struct { uint32_t status, irq, tx[3], count, cleared, reset_count; } fake_t;
static uint32_t read_reg(void *context, uint32_t offset)
{
    fake_t *f = context;
    if (offset == CODEC_SR) { return f->status; }
    if (offset == CODEC_ISR) { return f->irq; }
    return 0U;
}
static void write_reg(void *context, uint32_t offset, uint32_t value)
{
    fake_t *f = context;
    if (offset == CODEC_RESET) { ++f->reset_count; }
    if (offset == CODEC_ISR) { f->irq ^= value; f->cleared |= value; }
    if (offset == CODEC_TX && f->count < 3U) { f->tx[f->count++] = value; }
}
static bool setup(sonar_codec_bus_t *bus, fake_t *f)
{
    sonar_codec_bus_io_t io = {f, read_reg, write_reg};
    memset(f, 0, sizeof(*f)); f->status = 0xc0U;
    return sonar_codec_bus_init(bus, &io);
}
static bool dynamic_address_then_stop(void)
{
    sonar_codec_bus_t bus; fake_t f; sonar_codec_event_t event;
    uint8_t data[2] = {5U, 127U}; CHECK(setup(&bus, &f));
    CHECK(sonar_codec_bus_write(&bus, 26U, data, 7U)); data[1] = 0U;
    CHECK(f.count == 1U && f.tx[0] == 0x134U);
    CHECK(!sonar_codec_bus_event(&bus, &event));
    f.status = 0xc4U; f.irq = 0x10U;
    CHECK(!sonar_codec_bus_event(&bus, &event));
    CHECK(f.count == 3U && f.tx[1] == 5U && f.tx[2] == 0x27fU && (f.cleared & 0x10U) != 0U);
    f.status = 0xc0U; f.irq = 0x10U;
    CHECK(sonar_codec_bus_event(&bus, &event));
    return event.token == 7U && event.acknowledged && !bus.active;
}
static bool stale_bnb_is_not_completion(void)
{
    sonar_codec_bus_t bus; fake_t f; sonar_codec_event_t event;
    uint8_t data[2] = {0U, 0U}; CHECK(setup(&bus, &f)); f.irq = 0x10U;
    CHECK(sonar_codec_bus_write(&bus, 26U, data, 1U)); f.irq = 0x10U;
    CHECK(!sonar_codec_bus_event(&bus, &event)); return bus.active && f.count == 1U;
}
static bool errors_beat_idle(void)
{
    for (uint32_t error = 1U; error <= 2U; ++error) {
        sonar_codec_bus_t bus; fake_t f; sonar_codec_event_t event; uint8_t data[2] = {0U, 0U};
        CHECK(setup(&bus, &f)); CHECK(sonar_codec_bus_write(&bus, 26U, data, 1U));
        f.irq = error | 0x10U; CHECK(sonar_codec_bus_event(&bus, &event));
        CHECK(!event.acknowledged && !sonar_codec_bus_event(&bus, &event));
        CHECK(sonar_codec_bus_abort(&bus));
    }
    return true;
}
static bool busy_and_second_write_rejected(void)
{
    sonar_codec_bus_t bus; fake_t f; uint8_t data[2] = {0U, 0U}; CHECK(setup(&bus, &f));
    f.status |= 4U; CHECK(!sonar_codec_bus_write(&bus, 26U, data, 1U)); f.status = 0xc0U;
    CHECK(sonar_codec_bus_write(&bus, 26U, data, 1U));
    return !sonar_codec_bus_write(&bus, 26U, data, 2U) && f.count == 1U;
}
static bool stuck_abort_quarantines(void)
{
    sonar_codec_bus_t bus; fake_t f; uint8_t data[2] = {0U, 0U}; CHECK(setup(&bus, &f));
    CHECK(sonar_codec_bus_write(&bus, 26U, data, 1U)); f.status |= 4U;
    CHECK(!sonar_codec_bus_abort(&bus)); f.status = 0xc0U;
    return bus.quarantined && !sonar_codec_bus_write(&bus, 26U, data, 2U);
}
static bool completion_requires_empty_fifo(void)
{
    sonar_codec_bus_t bus; fake_t f; sonar_codec_event_t event; uint8_t data[2] = {0U, 0U};
    CHECK(setup(&bus, &f)); CHECK(sonar_codec_bus_write(&bus, 26U, data, 1U));
    f.status = 0xc4U; CHECK(!sonar_codec_bus_event(&bus, &event));
    f.status = 0x40U; f.irq = 0x10U;
    return !sonar_codec_bus_event(&bus, &event);
}
static bool output_bits_are_independent(void)
{
    uint32_t output = 0U;
    CHECK(sonar_gpio_merge(1U, 2U, 2U, &output) && output == 3U);
    CHECK(sonar_gpio_merge(output, 1U, 0U, &output) && output == 2U);
    CHECK(sonar_gpio_merge(output, 1U, 1U, &output) && output == 3U);
    return sonar_gpio_merge(output, 2U, 0U, &output) && output == 1U;
}
static bool rejects_unknown_output_bits(void)
{
    uint32_t output = 7U;
    CHECK(!sonar_gpio_merge(0U, 4U, 4U, &output));
    CHECK(!sonar_gpio_merge(0U, 1U, 2U, &output));
    CHECK(!sonar_gpio_merge(4U, 1U, 0U, &output)); return output == 7U;
}
static bool invalid_bus_arguments(void)
{
    sonar_codec_bus_t bus; fake_t f; uint8_t data[2] = {0U, 0U}; CHECK(setup(&bus, &f));
    CHECK(!sonar_codec_bus_write(&bus, 128U, data, 1U));
    CHECK(!sonar_codec_bus_write(&bus, 26U, NULL, 1U));
    CHECK(!sonar_codec_bus_write(&bus, 26U, data, 0U));
    CHECK(!sonar_codec_bus_event(NULL, NULL)); CHECK(!sonar_codec_bus_abort(NULL));
    return !sonar_codec_bus_init(NULL, &bus.io) && !sonar_codec_bus_init(&bus, NULL);
}
sonar_test_result_t sonar_codec_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"codec bus dynamic START/STOP", dynamic_address_then_stop},
        {"codec bus stale BNB", stale_bnb_is_not_completion},
        {"codec bus NACK/arbitration", errors_beat_idle},
        {"codec bus exclusive transfer", busy_and_second_write_rejected},
        {"codec bus quarantine", stuck_abort_quarantines},
        {"codec bus completion FIFO check", completion_requires_empty_fifo},
        {"GPIO codec and trigger isolation", output_bits_are_independent},
        {"GPIO rejects unknown bits", rejects_unknown_output_bits},
        {"codec bus invalid arguments", invalid_bus_arguments}
    };
    sonar_test_result_t result = {0U, 0U};
    for (unsigned i = 0U; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
