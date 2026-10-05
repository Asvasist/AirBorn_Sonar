#ifndef SONAR_SCAN360_H
#define SONAR_SCAN360_H

#include <stdbool.h>
#include <stdint.h>

/* Mechanical calibration agreed for this setup: one full 360-degree turn
 * equals exactly 800 Tic position units at the configured step mode. */
#define SONAR_SCAN360_REVOLUTION_STEPS 19902U
#define SONAR_SCAN360_MAX_POSITIONS    1000U

typedef struct {
    uint32_t positions;
    uint32_t moves_started;
    int8_t direction;
} sonar_scan360_t;

/* signed_positions: +N = forward, -N = reverse, 1 <= N <= 800. */
bool sonar_scan360_configure(sonar_scan360_t *scan, int32_t signed_positions);
void sonar_scan360_begin(sonar_scan360_t *scan);
uint32_t sonar_scan360_positions(const sonar_scan360_t *scan);
uint32_t sonar_scan360_capture_index(const sonar_scan360_t *scan);
int32_t sonar_scan360_next_delta(const sonar_scan360_t *scan);
void sonar_scan360_mark_move_started(sonar_scan360_t *scan);
bool sonar_scan360_complete(const sonar_scan360_t *scan);

#endif
