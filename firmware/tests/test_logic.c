/* Host tests for the hardware-independent sequencing, buffering and protocol code. */
#include "sonar_component_selftest.h"
#include "sonar_console_buffer.h"
#include "sonar_control_protocol.h"
#include "sonar_cycle.h"
#include "sonar_experiment_config.h"
#include "sonar_frames.h"
#include "sonar_number.h"
#include "sonar_scan360.h"
#include "sonar_wav.h"
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

/* ---- sonar_cycle ------------------------------------------------------- */

typedef struct {
    int capture_result;
    bool move_ok, stop_ok;
    int captures, moves, stops;
} fake_io_t;
static fake_io_t io_state;

static int fake_capture(void *context, uint32_t cycle, uint64_t *trigger)
{
    (void)context;
    (void)cycle;
    (void)trigger;
    ++io_state.captures;
    return io_state.capture_result;
}
static bool fake_move(void *context, int32_t steps)
{
    (void)context;
    (void)steps;
    ++io_state.moves;
    return io_state.move_ok;
}
static bool fake_stop(void *context)
{
    (void)context;
    ++io_state.stops;
    return io_state.stop_ok;
}

static bool cycle_setup(sonar_cycle_t *c)
{
    const sonar_cycle_config_t cfg = {.tx_timeout_us = 60000U,
                                      .motor_start_us = 2000000U,
                                      .settle_us = 2000000U,
                                      .capture_timeout_us = 500000U,
                                      .motion_timeout_us = 10000000U};
    const sonar_cycle_io_t io = {NULL, fake_capture, fake_move, fake_stop};
    io_state = (fake_io_t){1, true, true, 0, 0, 0};
    return sonar_cycle_init(c, &cfg, &io) && sonar_cycle_start(c, 10, 0U);
}

static bool cycle_runs_full_sequence(void)
{
    sonar_cycle_t c;
    CHECK(cycle_setup(&c));
    sonar_cycle_poll(&c, 0U, false, false, false, false);
    CHECK(c.state == CYCLE_ACQUIRE && c.cycle == 1U);
    sonar_cycle_poll(&c, 50000U, true, true, false, false);
    CHECK(c.state == CYCLE_PRE_MOVE);
    sonar_cycle_poll(&c, 1999999U, false, false, false, false);
    CHECK(c.state == CYCLE_PRE_MOVE && io_state.moves == 0);
    sonar_cycle_poll(&c, 2000000U, false, false, false, false); /* Deadline counts from trigger. */
    CHECK(c.state == CYCLE_MOVING && io_state.moves == 1);
    sonar_cycle_poll(&c, 2500000U, false, false, true, false);
    CHECK(c.state == CYCLE_SETTLING);
    sonar_cycle_poll(&c, 4500000U, false, false, false, false);
    return c.state == CYCLE_WAIT_BUFFER;
}

static bool cycle_waits_for_buffer_and_times_out_capture(void)
{
    sonar_cycle_t c;
    CHECK(cycle_setup(&c));
    io_state.capture_result = 0; /* DDR full. */
    sonar_cycle_poll(&c, 0U, false, false, false, false);
    CHECK(c.state == CYCLE_WAIT_BUFFER && c.cycle == 0U);
    io_state.capture_result = 1;
    sonar_cycle_poll(&c, 10U, false, false, false, false);
    CHECK(c.state == CYCLE_ACQUIRE);
    sonar_cycle_poll(&c, 10U + 60000U, false, true, false, false);
    CHECK(c.state == CYCLE_ACQUIRE);
    sonar_cycle_poll(&c, 10U + 500000U, false, false, false, false);
    return c.state == CYCLE_FAULT && strcmp(c.fault_reason, "CAPTURE_TIMEOUT") == 0 &&
           c.fault_state == CYCLE_ACQUIRE && io_state.stops == 1;
}

static bool cycle_stop_drains_in_flight_capture(void)
{
    sonar_cycle_t c;
    CHECK(cycle_setup(&c));
    sonar_cycle_poll(&c, 0U, false, false, false, false);
    CHECK(sonar_cycle_stop(&c, 1000U) && c.state == CYCLE_ACQUIRE);
    sonar_cycle_poll(&c, 50000U, true, true, false, false);
    return c.state == CYCLE_STOPPED && io_state.moves == 0;
}

static bool cycle_faults_on_clock_and_peripheral_errors(void)
{
    sonar_cycle_t c;
    CHECK(cycle_setup(&c));
    sonar_cycle_poll(&c, 100U, false, false, false, false);
    sonar_cycle_poll(&c, 99U, false, false, false, false);
    CHECK(c.state == CYCLE_FAULT && strcmp(c.fault_reason, "CLOCK_BACKWARDS") == 0);
    CHECK(cycle_setup(&c));
    sonar_cycle_poll(&c, 0U, false, false, false, true);
    CHECK(c.state == CYCLE_FAULT && !sonar_cycle_start(&c, 10, 1U));
    return strcmp(sonar_cycle_name((sonar_cycle_state_t)99), "INVALID") == 0;
}

/* ---- sonar_frames ------------------------------------------------------ */

static bool frames_round_trip_oldest_first(void)
{
    static sonar_frames_t pool;
    memset(&pool, 0, sizeof(pool));
    int a = sonar_frames_acquire(&pool), b = sonar_frames_acquire(&pool);
    CHECK(a == 0 && b == 1);
    sonar_frame_meta_t meta = {.sequence = 7U};
    CHECK(sonar_frames_publish(&pool, (unsigned)b, &meta));
    meta.sequence = 8U;
    CHECK(sonar_frames_publish(&pool, (unsigned)a, &meta));
    CHECK(!sonar_frames_publish(&pool, (unsigned)a, &meta)); /* Not FILLING any more. */
    CHECK(sonar_frames_latest(&pool) == 8U);
    CHECK(sonar_frames_take(&pool, 7U) == b); /* Oldest within the batch limit. */
    CHECK(sonar_frames_take(&pool, 7U) == -1);
    CHECK(sonar_frames_finish(&pool, (unsigned)b, false)); /* NAK keeps the frame. */
    CHECK(sonar_frames_take(&pool, 8U) == b);
    CHECK(sonar_frames_finish(&pool, (unsigned)b, true));
    return pool.slots[b].state == FRAME_FREE && !sonar_frames_finish(&pool, (unsigned)b, true);
}

static bool frames_pool_exhaustion(void)
{
    static sonar_frames_t pool;
    memset(&pool, 0, sizeof(pool));
    for (unsigned i = 0U; i < SONAR_ACQ_POOL_COUNT; ++i) {
        CHECK(sonar_frames_acquire(&pool) == (int)i);
    }
    return sonar_frames_acquire(&pool) == -1;
}

static bool crc32_and_header(void)
{
    uint8_t header[SONAR_WIRE_HEADER];
    const sonar_frame_meta_t meta = {.sequence = 0x01020304U, .flags = 0x100U};
    CHECK(sonar_crc32((const uint8_t *)"123456789", 9U) == 0xcbf43926U); /* CRC-32/ISO-HDLC */
    sonar_frame_header(header, &meta, 1000U, 0xdeadbeefU);
    CHECK(memcmp(header, "ASN2", 4) == 0 && header[6] == SONAR_WIRE_HEADER);
    CHECK(header[12] == 0x04U && header[15] == 0x01U && header[72] == 0xefU);
    return sonar_get_u32(header + 76) == sonar_crc32(header, 76U);
}

/* ---- sonar_control_protocol --------------------------------------------- */

static bool control_header_round_trip_and_rejection(void)
{
    uint8_t header[SONAR_CONTROL_HEADER];
    const uint8_t payload[3] = {1U, 2U, 3U};
    uint32_t op = 0U, id = 0U, bytes = 0U, crc = 0U;
    sonar_control_header(header, CTRL_APPLY, 42U, payload, sizeof(payload));
    CHECK(sonar_control_decode(header, &op, &id, &bytes, &crc));
    CHECK(op == CTRL_APPLY && id == 42U && bytes == 3U && crc == sonar_crc32(payload, 3U));
    header[9] ^= 1U; /* Corrupt the opcode: header CRC must catch it. */
    CHECK(!sonar_control_decode(header, &op, &id, &bytes, &crc));
    sonar_control_header(header, CTRL_APPLY, 1U, NULL, 0U);
    sonar_put_u32(header + 16, SONAR_CONTROL_MAX_PAYLOAD + 1U);
    sonar_put_u32(header + 24, sonar_crc32(header, 24U));
    return !sonar_control_decode(header, &op, &id, &bytes, &crc); /* Oversized payload. */
}

/* ---- sonar_console_buffer ------------------------------------------------ */

static bool console_buffer_returns_complete_lines_and_reports_loss(void)
{
    static sonar_console_buffer_t buffer;
    uint8_t out[64];
    uint64_t cursor = 0U, lost = 0U;
    memset(&buffer, 0, sizeof(buffer));
    for (const char *p = "ok\npartial"; *p != '\0'; ++p) {
        sonar_console_buffer_put(&buffer, (uint8_t)*p);
    }
    CHECK(sonar_console_buffer_read(&buffer, &cursor, UINT64_MAX, out, sizeof(out), &lost) == 3U);
    CHECK(memcmp(out, "ok\n", 3) == 0 && lost == 0U);
    CHECK(sonar_console_buffer_read(&buffer, &cursor, UINT64_MAX, out, sizeof(out), &lost) == 0U);
    for (uint32_t i = 0U; i < SONAR_CONSOLE_BUFFER_BYTES; ++i) {
        sonar_console_buffer_put(&buffer, '\n');
    }
    CHECK(sonar_console_buffer_read(&buffer, &cursor, UINT64_MAX, out, sizeof(out), &lost) == 0U);
    return lost == 7U &&
           cursor == buffer.written - SONAR_CONSOLE_BUFFER_BYTES; /* "partial" overwritten. */
}

/* ---- sonar_number, sonar_scan360 ---------------------------------------- */

static bool feed(sonar_number_t *n, const char *text, int32_t *value)
{
    sonar_number_result_t result = NUMBER_WAIT;
    for (; *text != '\0'; ++text) {
        result = sonar_number_feed(n, (uint8_t)*text, value);
    }
    return result == NUMBER_OK;
}

static bool number_parser(void)
{
    sonar_number_t n = {0};
    int32_t value = 0;
    CHECK(feed(&n, "-30\r", &value) && value == -30);
    CHECK(feed(&n, "12\b5\r", &value) && value == 15);
    CHECK(!feed(&n, "1-2\r", &value) && !feed(&n, "\r", &value) &&
          !feed(&n, "99999999999\r", &value));
    return !feed(&n, "12345678901234567\r", &value) && value == 15;
}

static bool scan_moves_sum_to_one_revolution(void)
{
    sonar_scan360_t scan;
    for (int32_t n = 1; n <= (int32_t)SONAR_SCAN360_MAX_POSITIONS; ++n) {
        int64_t total = 0;
        CHECK(sonar_scan360_configure(&scan, (n & 1) != 0 ? n : -n));
        while (!sonar_scan360_complete(&scan)) {
            total += sonar_scan360_next_delta(&scan);
            sonar_scan360_mark_move_started(&scan);
        }
        CHECK(total == ((n & 1) != 0 ? 1 : -1) * (int64_t)SONAR_SCAN360_REVOLUTION_STEPS);
        CHECK(sonar_scan360_next_delta(&scan) == 0 && sonar_scan360_capture_index(&scan) == 0U);
    }
    return !sonar_scan360_configure(&scan, 0) &&
           !sonar_scan360_configure(&scan, (int32_t)SONAR_SCAN360_MAX_POSITIONS + 1);
}

/* ---- sonar_experiment_config, sonar_wav --------------------------------- */

static bool chirp_parameters(void)
{
    sonar_experiment_config_t c = {SONAR_TX_GENERATE, 2500U, 5600U, 35000U, 40U, 0U, 1U};
    sonar_chirp_values_t v = {0};
    CHECK(sonar_experiment_calculate(&c, 0U, &v));
    CHECK(v.total_samples == 3360U && v.duration_us == 35000U && v.amplitude == 13107U);
    CHECK(v.start_phase_inc == 111848107U && v.phase_inc_step > 0);
    c.stop_hz = SONAR_AUDIO_HZ / 2U; /* Nyquist is out of range. */
    CHECK(!sonar_experiment_calculate(&c, 0U, &v) && v.total_samples == 3360U);
    c = (sonar_experiment_config_t){SONAR_TX_WAV, 0U, 0U, 0U, 100U, 3U, 2U};
    CHECK(sonar_experiment_calculate(&c, 4704U, &v) && v.duration_us == 49000U);
    return !sonar_experiment_calculate(&c, SONAR_TX_MAX_SAMPLES + 1U, &v);
}

static bool wav_header_fields(void)
{
    uint8_t h[SONAR_WAV_HEADER_BYTES];
    CHECK(sonar_wav_write_header(h, SONAR_PCM_FRAMES));
    CHECK(memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVEfmt ", 8) == 0 &&
          memcmp(h + 36, "data", 4) == 0);
    CHECK(sonar_get_u32(h + 24) == 96000U && h[22] == 16U && h[34] == 16U);
    return sonar_get_u32(h + 40) == SONAR_PCM_BYTES &&
           sonar_get_u32(h + 4) == 36U + SONAR_PCM_BYTES;
}

sonar_test_result_t sonar_logic_selftest_run(sonar_test_report_fn report)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"cycle runs trigger, move, settle sequence", cycle_runs_full_sequence},
        {"cycle waits for buffer and times out capture",
         cycle_waits_for_buffer_and_times_out_capture},
        {"cycle stop drains in-flight capture", cycle_stop_drains_in_flight_capture},
        {"cycle faults on clock and peripheral errors",
         cycle_faults_on_clock_and_peripheral_errors},
        {"frames round trip oldest first with NAK retry", frames_round_trip_oldest_first},
        {"frames pool exhaustion", frames_pool_exhaustion},
        {"crc32 check value and wire header", crc32_and_header},
        {"control header round trip and rejection", control_header_round_trip_and_rejection},
        {"console buffer lines and loss report",
         console_buffer_returns_complete_lines_and_reports_loss},
        {"number parser", number_parser},
        {"scan moves sum to one revolution", scan_moves_sum_to_one_revolution},
        {"chirp parameter calculation", chirp_parameters},
        {"wav header fields", wav_header_fields},
    };
    sonar_test_result_t result = {0U, 0U};
    for (size_t i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        report(tests[i].name, passed);
        if (passed) {
            ++result.passed;
        } else {
            ++result.failed;
        }
    }
    return result;
}
