#include "sonar_selftest.h"
#include "sonar_config.h"
#include "sonar_health.h"
#include "sonar_profile.h"
#include <stddef.h>

#define CHECK(condition) do { if (!(condition)) { return false; } } while (0)

static bool default_config(void)
{
    sonar_config_t config = sonar_config_default();
    sonar_timing_t ticks;
    CHECK(sonar_config_validate(&config, 1000U, &ticks));
    CHECK(ticks.heartbeat == SONAR_HEARTBEAT_MS);
    CHECK(ticks.timeout == SONAR_TIMEOUT_MS);
    return ticks.report == SONAR_REPORT_MS;
}

static bool fractional_ticks_round_up(void)
{
    uint32_t ticks = 0U;
    CHECK(sonar_ms_to_ticks(1U, 100U, &ticks) && ticks == 1U);
    return sonar_ms_to_ticks(101U, 100U, &ticks) && ticks == 11U;
}

static bool invalid_conversion_preserves_output(void)
{
    uint32_t ticks = 77U;
    CHECK(!sonar_ms_to_ticks(0U, 1000U, &ticks));
    CHECK(!sonar_ms_to_ticks(1U, 0U, &ticks));
    CHECK(!sonar_ms_to_ticks(UINT32_MAX, UINT32_MAX, &ticks));
    CHECK(!sonar_ms_to_ticks(1U, 1000U, NULL));
    return ticks == 77U;
}

static bool invalid_config_preserves_output(void)
{
    sonar_config_t config = {100U, 200U, 1000U};
    sonar_timing_t ticks = {7U, 8U, 9U};
    CHECK(!sonar_config_validate(&config, 1000U, &ticks));
    config.timeout_ms = 500U;
    config.report_ms = 0U;
    CHECK(!sonar_config_validate(&config, 1000U, &ticks));
    CHECK(!sonar_config_validate(NULL, 1000U, &ticks));
    CHECK(!sonar_config_validate(&config, 1000U, NULL));
    return ticks.heartbeat == 7U && ticks.timeout == 8U && ticks.report == 9U;
}

static bool coarse_tick_rate_rejects_lost_margin(void)
{
    sonar_config_t config = {1U, 3U, 100U};
    sonar_timing_t ticks;
    return !sonar_config_validate(&config, 10U, &ticks);
}

static bool health_waits_for_first_event(void)
{
    sonar_health_t health;
    CHECK(sonar_health_init(&health, 20U, 500U));
    CHECK(health.state == SONAR_WAITING && health.received == 0U);
    return sonar_health_poll(&health, 519U) == SONAR_WAITING;
}

static bool startup_timeout_is_inclusive(void)
{
    sonar_health_t health;
    CHECK(sonar_health_init(&health, 20U, 500U));
    return sonar_health_poll(&health, 520U) == SONAR_FAULT_TIMEOUT;
}

static bool valid_events_advance_state(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 100U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    CHECK(sonar_health_accept(&health, &event, 105U) == SONAR_RUNNING);
    event.sequence = 1U;
    event.tick = 200U;
    CHECK(sonar_health_accept(&health, &event, 209U) == SONAR_RUNNING);
    return health.received == 2U && health.last_tick == 200U;
}

static bool missing_event_is_latched(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {1U, 100U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    CHECK(sonar_health_accept(&health, &event, 100U) == SONAR_FAULT_SEQUENCE);
    event.sequence = 0U;
    CHECK(sonar_health_accept(&health, &event, 110U) == SONAR_FAULT_SEQUENCE);
    return health.received == 0U;
}

static bool duplicate_event_fails(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 100U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    CHECK(sonar_health_accept(&health, &event, 100U) == SONAR_RUNNING);
    return sonar_health_accept(&health, &event, 101U) == SONAR_FAULT_SEQUENCE;
}

static bool stale_queued_event_fails(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 100U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    return sonar_health_accept(&health, &event, 600U) == SONAR_FAULT_TIMEOUT;
}

static bool late_event_cannot_hide_gap(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 500U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    return sonar_health_accept(&health, &event, 500U) == SONAR_FAULT_TIMEOUT;
}

static bool queued_events_cannot_hide_supervisor_stall(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 100U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    return sonar_health_accept(&health, &event, 500U) == SONAR_FAULT_TIMEOUT;
}

static bool future_timestamp_fails(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 101U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    return sonar_health_accept(&health, &event, 100U) == SONAR_FAULT_TIMEOUT;
}

static bool tick_wrap_is_supported(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {0U, 40U};
    CHECK(sonar_health_init(&health, UINT32_MAX - 59U, 500U));
    CHECK(sonar_health_accept(&health, &event, 45U) == SONAR_RUNNING);
    CHECK(sonar_health_poll(&health, 539U) == SONAR_RUNNING);
    return sonar_health_poll(&health, 540U) == SONAR_FAULT_TIMEOUT;
}

static bool sequence_wrap_is_supported(void)
{
    sonar_health_t health;
    sonar_heartbeat_t event = {UINT32_MAX, 100U};
    CHECK(sonar_health_init(&health, 0U, 500U));
    health.next_sequence = UINT32_MAX;
    CHECK(sonar_health_accept(&health, &event, 100U) == SONAR_RUNNING);
    event.sequence = 0U;
    event.tick = 200U;
    return sonar_health_accept(&health, &event, 200U) == SONAR_RUNNING;
}

static bool queue_fault_preserves_first_cause(void)
{
    sonar_health_t health;
    CHECK(sonar_health_init(&health, 0U, 500U));
    sonar_health_queue_fault(&health);
    CHECK(sonar_health_poll(&health, 1000U) == SONAR_FAULT_QUEUE);
    CHECK(sonar_health_init(&health, 0U, 500U));
    CHECK(sonar_health_poll(&health, 500U) == SONAR_FAULT_TIMEOUT);
    sonar_health_queue_fault(&health);
    return health.state == SONAR_FAULT_TIMEOUT;
}

static bool invalid_health_arguments_fail(void)
{
    sonar_health_t health;
    CHECK(!sonar_health_init(NULL, 0U, 500U));
    CHECK(!sonar_health_init(&health, 0U, 0U));
    CHECK(health.state == SONAR_FAULT_CONFIG);
    CHECK(!sonar_health_init(&health, 0U, UINT32_MAX));
    CHECK(sonar_health_init(&health, 0U, 500U));
    CHECK(sonar_health_accept(&health, NULL, 0U) == SONAR_FAULT_CONFIG);
    CHECK(sonar_health_poll(NULL, 0U) == SONAR_FAULT_CONFIG);
    return sonar_health_accept(NULL, NULL, 0U) == SONAR_FAULT_CONFIG;
}

static bool active_profile_matches(void)
{
    sonar_profile_t profile = {SONAR_EXPECTED_GPIO_BASE, SONAR_EXPECTED_GPIO_WIDTH,
        SONAR_EXPECTED_DMA_BASE, 1U, SONAR_EXPECTED_IIC_BASE};
    return sonar_profile_check(&profile) == 0U;
}

static bool archived_profile_is_flagged(void)
{
    sonar_profile_t profile = {SONAR_EXPECTED_GPIO_BASE, 1U,
        SONAR_EXPECTED_DMA_BASE, 1U, UINT32_C(0)};
    return sonar_profile_check(&profile) == (SONAR_PROFILE_GPIO | SONAR_PROFILE_IIC);
}

static bool unknown_profile_is_flagged(void)
{
    sonar_profile_t profile = {0U, 0U, 0U, 0U, 0U};
    const uint32_t all = SONAR_PROFILE_GPIO | SONAR_PROFILE_DMA | SONAR_PROFILE_IIC;
    CHECK(sonar_profile_check(&profile) == all);
    return sonar_profile_check(NULL) == all;
}

sonar_test_result_t sonar_selftest_run(sonar_test_report_fn report)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"default configuration", default_config},
        {"fractional tick rounding", fractional_ticks_round_up},
        {"invalid tick conversion", invalid_conversion_preserves_output},
        {"invalid configuration", invalid_config_preserves_output},
        {"coarse tick margin", coarse_tick_rate_rejects_lost_margin},
        {"wait for first heartbeat", health_waits_for_first_event},
        {"startup deadline", startup_timeout_is_inclusive},
        {"ordered heartbeat delivery", valid_events_advance_state},
        {"missing heartbeat and fault latch", missing_event_is_latched},
        {"duplicate heartbeat", duplicate_event_fails},
        {"stale queued heartbeat", stale_queued_event_fails},
        {"late heartbeat", late_event_cannot_hide_gap},
        {"supervisor stall", queued_events_cannot_hide_supervisor_stall},
        {"future timestamp", future_timestamp_fails},
        {"tick rollover", tick_wrap_is_supported},
        {"sequence rollover", sequence_wrap_is_supported},
        {"queue fault and first cause", queue_fault_preserves_first_cause},
        {"invalid health arguments", invalid_health_arguments_fail},
        {"active BD BSP profile", active_profile_matches},
        {"older one-bit BSP rejected", archived_profile_is_flagged},
        {"unknown BSP profile", unknown_profile_is_flagged}
    };
    sonar_test_result_t result = {0U, 0U};
    size_t i;
    for (i = 0U; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool passed = tests[i].run();
        if (passed) { ++result.passed; } else { ++result.failed; }
        if (report != NULL) { report(tests[i].name, passed); }
    }
    return result;
}
