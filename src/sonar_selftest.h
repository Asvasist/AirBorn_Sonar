#ifndef SONAR_SELFTEST_H
#define SONAR_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*sonar_test_report_fn)(const char *name, bool passed);
typedef struct { uint32_t passed; uint32_t failed; } sonar_test_result_t;

/* The same deterministic tests run on the host and before the target scheduler. */
sonar_test_result_t sonar_selftest_run(sonar_test_report_fn report);

#endif
