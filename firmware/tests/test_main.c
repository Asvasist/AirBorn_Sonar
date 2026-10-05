#include <stdio.h>
#include "sonar_selftest.h"
#include "sonar_mic_selftest.h"
#include "sonar_component_selftest.h"
#include "sonar_scan_selftest.h"

static void report(const char *name, bool passed)
{
    printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

int main(void)
{
    sonar_test_result_t result = sonar_selftest_run(report);
    sonar_test_result_t microphone = sonar_mic_selftest_run(report);
    result.passed += microphone.passed;
    result.failed += microphone.failed;
    sonar_test_result_t motor = sonar_motor_selftest_run(report);
    sonar_test_result_t speaker = sonar_speaker_selftest_run(report);
    result.passed += motor.passed + speaker.passed;
    result.failed += motor.failed + speaker.failed;
    sonar_test_result_t drivers = sonar_driver_selftest_run(report);
    result.passed += drivers.passed;
    result.failed += drivers.failed;
    sonar_test_result_t codec = sonar_codec_selftest_run(report);
    result.passed += codec.passed;
    result.failed += codec.failed;
    sonar_test_result_t scan = sonar_scan_selftest_run(report);
    result.passed += scan.passed;
    result.failed += scan.failed;
    printf("%lu passed; %lu failed\n", (unsigned long)result.passed,
           (unsigned long)result.failed);
    return result.failed == 0U ? 0 : 1;
}
