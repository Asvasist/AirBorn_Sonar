#include "sonar_scan360.h"
#include <stddef.h>

bool sonar_scan360_configure(sonar_scan360_t *scan, int32_t signed_positions)
{
    if (scan == NULL || signed_positions == 0 ||
        signed_positions < -(int32_t)SONAR_SCAN360_MAX_POSITIONS ||
        signed_positions >  (int32_t)SONAR_SCAN360_MAX_POSITIONS) {
        return false;
    }
    scan->positions = signed_positions < 0 ? (uint32_t)(-signed_positions) : (uint32_t)signed_positions;
    scan->direction = signed_positions < 0 ? -1 : 1;
    scan->moves_started = 0U;
    return true;
}

void sonar_scan360_begin(sonar_scan360_t *scan)
{
    if (scan != NULL) { scan->moves_started = 0U; }
}

uint32_t sonar_scan360_positions(const sonar_scan360_t *scan)
{
    return scan != NULL ? scan->positions : 0U;
}

uint32_t sonar_scan360_capture_index(const sonar_scan360_t *scan)
{
    if (scan == NULL || scan->positions == 0U || scan->moves_started >= scan->positions) { return 0U; }
    return scan->moves_started + 1U;
}

int32_t sonar_scan360_next_delta(const sonar_scan360_t *scan)
{
    if (scan == NULL || scan->positions == 0U || scan->moves_started >= scan->positions) { return 0; }

    const uint32_t n = scan->positions;
    const uint32_t k = scan->moves_started + 1U;

    /* Round cumulative targets, then subtract adjacent targets. This makes
     * the N relative moves sum to exactly one revolution even when it is not
     * divisible by N. */
    const uint32_t previous = (uint32_t)(((uint64_t)(k - 1U) * SONAR_SCAN360_REVOLUTION_STEPS + n / 2U) / n);
    const uint32_t next = (uint32_t)(((uint64_t)k * SONAR_SCAN360_REVOLUTION_STEPS + n / 2U) / n);
    const int32_t delta = (int32_t)(next - previous);
    return scan->direction < 0 ? -delta : delta;
}

void sonar_scan360_mark_move_started(sonar_scan360_t *scan)
{
    if (scan != NULL && scan->moves_started < scan->positions) { ++scan->moves_started; }
}

bool sonar_scan360_complete(const sonar_scan360_t *scan)
{
    return scan != NULL && scan->positions != 0U && scan->moves_started >= scan->positions;
}
