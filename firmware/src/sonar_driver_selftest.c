#include "sonar_component_selftest.h"
#include "sonar_ps_i2c.h"
#include "sonar_tic.h"
#include <stddef.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { return false; } } while (0)
typedef struct {
    uint32_t tick, regs[11], irq, busy, data_writes, address_at_count, last_address;
    uint32_t rx_count, rx_index, extra_rx, fifo[16], fail_after_ticks;
    bool started, read_mode;
} registers_t;
static uint32_t read_reg(void *context, uint32_t offset)
{
    registers_t *r = context;
    if (offset == PS_I2C_ISR) { return r->started ? r->irq : 0U; }
    if (offset == PS_I2C_SR) {
        return r->busy | ((r->read_mode && r->rx_index < r->rx_count + r->extra_rx) ? PS_I2C_RX_VALID : 0U);
    }
    if (offset == PS_I2C_DATA) { ++r->rx_index; return r->rx_index; }
    return r->regs[offset / 4U];
}
static void write_reg(void *context, uint32_t offset, uint32_t value)
{
    registers_t *r = context;
    if (offset == PS_I2C_CR) {
        r->read_mode = (value & 1U) != 0U;
        if ((value & 0x40U) != 0U) { r->rx_index = 0U; r->started = false; }
    }
    if (offset == PS_I2C_DATA && r->data_writes < 16U) { r->fifo[r->data_writes++] = value; }
    if (offset == PS_I2C_SIZE) { r->rx_count = value; }
    if (offset == PS_I2C_ADDR) {
        r->started = true; r->address_at_count = r->data_writes; r->last_address = value;
    }
    r->regs[offset / 4U] = value;
}
static uint32_t reg_now(void *context) { return ((registers_t *)context)->tick; }
static void reg_wait(void *context) { ++((registers_t *)context)->tick; }
static bool bus_setup(sonar_ps_i2c_t *bus, registers_t *r)
{
    sonar_ps_i2c_io_t io = {r, read_reg, write_reg, reg_now, reg_wait};
    memset(r, 0, sizeof(*r)); r->irq = PS_I2C_COMPLETE;
    return sonar_ps_i2c_init(bus, &io, 111111115U, 20000U, 10U);
}
static bool fifo_precedes_address(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data[5] = {0xe3U, 1U, 2U, 3U, 4U};
    CHECK(bus_setup(&bus, &r)); CHECK(sonar_ps_i2c_write(&bus, 0x0eU, data, 5U));
    return r.address_at_count == 5U && r.last_address == 0x0eU && r.fifo[0] == 0xe3U && r.fifo[4] == 4U;
}
static bool bus_errors_win_over_completion(void)
{
    static const uint32_t errors[] = {4U, 8U, 32U, 64U, 128U, 512U};
    for (unsigned i = 0U; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        sonar_ps_i2c_t bus; registers_t r; uint8_t data = 0x8cU;
        CHECK(bus_setup(&bus, &r)); r.irq |= errors[i];
        CHECK(!sonar_ps_i2c_write(&bus, 14U, &data, 1U));
        CHECK(bus.last_irq == (errors[i] | 1U) && !bus.quarantined);
    }
    return true;
}
static bool bus_missing_completion_bounded(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data = 0x8cU;
    CHECK(bus_setup(&bus, &r)); r.irq = 0U;
    CHECK(!sonar_ps_i2c_write(&bus, 14U, &data, 1U));
    return r.tick == 10U && !bus.quarantined;
}
static bool bus_busy_quarantined(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data = 0x8cU;
    CHECK(bus_setup(&bus, &r)); r.busy = PS_I2C_BUSY;
    CHECK(!sonar_ps_i2c_write(&bus, 14U, &data, 1U));
    CHECK(bus.quarantined && r.data_writes == 0U);
    r.busy = 0U; CHECK(!sonar_ps_i2c_write(&bus, 14U, &data, 1U));
    return r.data_writes == 0U;
}
static bool bus_reads_exact_fifo_length(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data[4] = {0U};
    CHECK(bus_setup(&bus, &r)); CHECK(sonar_ps_i2c_read(&bus, 14U, data, 4U));
    return data[0] == 1U && data[3] == 4U && r.last_address == 14U && r.read_mode;
}
static bool bus_rejects_extra_received_byte(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data[4] = {0U};
    CHECK(bus_setup(&bus, &r)); r.extra_rx = 1U;
    return !sonar_ps_i2c_read(&bus, 14U, data, 4U);
}
static bool bus_validates_lengths_and_address(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data[17] = {0U};
    CHECK(bus_setup(&bus, &r)); CHECK(!sonar_ps_i2c_write(&bus, 128U, data, 1U));
    CHECK(!sonar_ps_i2c_write(&bus, 14U, data, 0U));
    CHECK(!sonar_ps_i2c_write(&bus, 14U, data, 17U));
    CHECK(!sonar_ps_i2c_read(&bus, 14U, NULL, 1U));
    CHECK(!sonar_ps_i2c_write(NULL, 14U, data, 1U));
    return r.data_writes == 0U;
}
static bool bus_tick_rollover(void)
{
    sonar_ps_i2c_t bus; registers_t r; uint8_t data = 0x8cU;
    CHECK(bus_setup(&bus, &r)); r.tick = UINT32_MAX - 4U; r.irq = 0U;
    CHECK(!sonar_ps_i2c_write(&bus, 14U, &data, 1U)); return r.tick == 5U;
}
static bool bus_divider_matches_reference(void)
{
    sonar_ps_i2c_t bus; registers_t r; CHECK(bus_setup(&bus, &r));
    return bus.divider == 0xfe00U && r.regs[PS_I2C_IDR / 4U] == 0x2ffU &&
        r.regs[PS_I2C_TIMEOUT / 4U] == 255U;
}
static bool bus_invalid_config_has_no_io(void)
{
    sonar_ps_i2c_t bus; registers_t r; sonar_ps_i2c_io_t io;
    CHECK(bus_setup(&bus, &r)); io = bus.io;
    CHECK(!sonar_ps_i2c_init(&bus, &io, 0U, 20000U, 10U));
    CHECK(!sonar_ps_i2c_init(&bus, &io, 111111115U, 0U, 10U));
    CHECK(!sonar_ps_i2c_init(&bus, &io, 111111115U, 20000U, 0U));
    io.wait = NULL; return !sonar_ps_i2c_init(&bus, &io, 111111115U, 20000U, 10U);
}

typedef struct {
    uint8_t writes[80][5], lengths[80], offset;
    unsigned calls, fail_call;
    uint32_t now;
    int32_t velocity, position;
    uint16_t errors;
    bool energized, fail_read, stuck_velocity;
} tic_fake_t;
static bool tic_write(void *context, uint8_t address, const uint8_t *data, uint32_t length)
{
    tic_fake_t *f = context;
    if (address != 14U || length > 5U || f->calls >= 80U) { return false; }
    memcpy(f->writes[f->calls], data, length); f->lengths[f->calls] = (uint8_t)length;
    ++f->calls;
    if (f->fail_call == f->calls) { return false; }
    if (data[0] == 0xa1U) { f->offset = data[1]; }
    if (data[0] == 0x85U) { f->energized = true; }
    if (data[0] == 0x86U) { f->energized = false; }
    if ((data[0] == 0x89U || (data[0] == 0xe3U && data[1] == 0U && data[2] == 0U &&
        data[3] == 0U && data[4] == 0U)) && !f->stuck_velocity) { f->velocity = 0; }
    return true;
}
static void little_endian(uint32_t value, uint8_t *data)
{
    for (unsigned i = 0U; i < 4U; ++i) { data[i] = (uint8_t)(value >> (i * 8U)); }
}
static bool tic_read(void *context, uint8_t address, uint8_t *data, uint32_t length)
{
    tic_fake_t *f = context;
    if (f->fail_read || address != 14U) { return false; }
    memset(data, 0, length);
    if (f->offset == 0U && length == 4U) {
        data[0] = f->energized ? 10U : 2U; data[1] = f->energized ? 1U : 0U;
        data[2] = (uint8_t)f->errors; data[3] = (uint8_t)(f->errors >> 8U);
    } else if (f->offset == 0x22U && length == 8U) {
        little_endian((uint32_t)f->position, data); little_endian((uint32_t)f->velocity, data + 4);
    } else { return false; }
    return true;
}
static uint32_t tic_now(void *context) { return ((tic_fake_t *)context)->now; }
static bool tic_setup(sonar_tic_t *tic, tic_fake_t *f)
{
    const sonar_tic_config_t config = {14U, 30000000U, 300000U, 300000U, 1000U, 1400U, 25U, 100U};
    const sonar_tic_io_t io = {f, tic_write, tic_read, tic_now};
    memset(f, 0, sizeof(*f)); return sonar_tic_init(tic, &config, &io);
}
static bool tic_start_order_and_signed_velocity(void)
{
    static const uint8_t commands[] = {0xe3U, 0xe6U, 0xeaU, 0xe9U, 0x8cU, 0x85U, 0x83U, 0xe3U};
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, -30000000));
    for (unsigned i = 0U; i < sizeof(commands); ++i) { CHECK(f.writes[i][0] == commands[i]); }
    uint8_t expected[4]; little_endian((uint32_t)-30000000, expected);
    return memcmp(f.writes[7] + 1, expected, 4U) == 0 && f.lengths[7] == 5U;
}
static bool tic_rejects_invalid_motion(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f));
    CHECK(!sonar_tic_start(&tic, 0)); CHECK(!sonar_tic_start(&tic, INT32_MIN));
    CHECK(!sonar_tic_start(&tic, 30000001)); return f.calls == 0U;
}
static bool tic_keepalive_is_periodic(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    unsigned before = f.calls; f.now = 25U; sonar_tic_poll(&tic);
    CHECK(f.writes[before][0] == 0x8cU && tic.last_keepalive == 25U);
    before = f.calls; f.now = 26U; sonar_tic_poll(&tic); return f.writes[before][0] == 0xa1U;
}
static bool tic_late_keepalive_latches_fault(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    unsigned before = f.calls; f.now = 100U; sonar_tic_poll(&tic);
    return tic.motor.state == MOTOR_FAULT && f.writes[before][0] != 0x8cU;
}
static bool tic_reads_signed_status(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); f.position = INT32_MIN; f.velocity = -30000000;
    CHECK(sonar_tic_status(&tic)); return tic.status.position == INT32_MIN && tic.status.velocity == -30000000;
}
static bool tic_stops_after_run_then_deenergizes(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    tic.last_keepalive = 999U; f.now = 1000U; sonar_tic_poll(&tic);
    return tic.motor.state == MOTOR_DONE && !f.energized && tic.motor.stop_confirmed;
}
static bool tic_does_not_infer_stop_from_delay(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    f.stuck_velocity = true; f.velocity = 10000; tic.last_keepalive = 999U; f.now = 1000U;
    sonar_tic_poll(&tic); return tic.motor.state == MOTOR_RUNNING && tic.stopping;
}
static bool tic_error_status_latches_fault(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    f.errors = 4U; sonar_tic_poll(&tic); return tic.motor.state == MOTOR_FAULT;
}
static bool tic_failed_status_is_not_valid(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_status(&tic));
    f.fail_read = true; CHECK(!sonar_tic_status(&tic)); return !tic.status_valid;
}
static bool tic_start_failure_attempts_stop(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); f.fail_call = 6U;
    CHECK(!sonar_tic_start(&tic, 30000000));
    return tic.motor.state == MOTOR_FAULT && f.writes[6][0] == 0x89U && f.writes[7][0] == 0x86U;
}
static bool tic_cancel_reports_unknown_on_failed_read(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    f.fail_read = true; sonar_tic_cancel(&tic);
    return tic.motor.fault == MOTOR_CANCELLED && !tic.motor.stop_confirmed && !f.energized;
}
static bool tic_validates_watchdog_margin(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f));
    sonar_tic_config_t config = tic.config; config.keepalive_ticks = config.watchdog_ticks;
    return !sonar_tic_init(&tic, &config, &tic.io);
}
static bool tic_halt_works_before_first_move(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f));
    CHECK(!tic.motor.stop_confirmed); f.energized = true; f.velocity = 10000;
    sonar_tic_cancel(&tic);
    return tic.motor.state == MOTOR_FAULT && tic.motor.stop_confirmed && !f.energized;
}
static bool tic_failed_keepalive_stops(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    f.fail_call = f.calls + 1U; f.now = 25U; sonar_tic_poll(&tic);
    return tic.motor.state == MOTOR_FAULT && tic.motor.fault == MOTOR_DRIVER_ERROR && !f.energized;
}
static bool tic_deadline_stops_stalled_motion(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); CHECK(sonar_tic_start(&tic, 30000000));
    f.now = 1400U; sonar_tic_poll(&tic);
    return tic.motor.fault == MOTOR_TIMEOUT && !f.energized;
}
static bool tic_fault_rejects_further_motion(void)
{
    sonar_tic_t tic; tic_fake_t f; CHECK(tic_setup(&tic, &f)); sonar_tic_cancel(&tic);
    unsigned before = f.calls; CHECK(!sonar_tic_start(&tic, 30000000));
    sonar_tic_poll(&tic); return f.calls == before;
}
sonar_test_result_t sonar_driver_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"PS I2C FIFO before address", fifo_precedes_address},
        {"PS I2C errors precede completion", bus_errors_win_over_completion},
        {"PS I2C bounded missing completion", bus_missing_completion_bounded},
        {"PS I2C stuck bus quarantine", bus_busy_quarantined},
        {"PS I2C exact read length", bus_reads_exact_fifo_length},
        {"PS I2C extra byte rejected", bus_rejects_extra_received_byte},
        {"PS I2C input validation", bus_validates_lengths_and_address},
        {"PS I2C tick wrap", bus_tick_rollover},
        {"PS I2C reference divider", bus_divider_matches_reference},
        {"PS I2C configuration validation", bus_invalid_config_has_no_io},
        {"Tic start sequence and signed bytes", tic_start_order_and_signed_velocity},
        {"Tic velocity limits", tic_rejects_invalid_motion},
        {"Tic periodic keepalive", tic_keepalive_is_periodic},
        {"Tic late keepalive faults", tic_late_keepalive_latches_fault},
        {"Tic signed status", tic_reads_signed_status},
        {"Tic observed controller stop", tic_stops_after_run_then_deenergizes},
        {"Tic stop is not a delay estimate", tic_does_not_infer_stop_from_delay},
        {"Tic controller error", tic_error_status_latches_fault},
        {"Tic failed read invalidates status", tic_failed_status_is_not_valid},
        {"Tic partial start stops", tic_start_failure_attempts_stop},
        {"Tic cancellation stop uncertainty", tic_cancel_reports_unknown_on_failed_read},
        {"Tic watchdog margin", tic_validates_watchdog_margin},
        {"Tic halt before first movement", tic_halt_works_before_first_move},
        {"Tic failed keepalive stops", tic_failed_keepalive_stops},
        {"Tic operation deadline", tic_deadline_stops_stalled_motion},
        {"Tic fault blocks motion", tic_fault_rejects_further_motion}
    };
    sonar_test_result_t result = {0U, 0U};
    for (unsigned i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
