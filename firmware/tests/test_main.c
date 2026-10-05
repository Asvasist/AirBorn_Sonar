#include <stdio.h>
#include "sonar_selftest.h"
#include "sonar_mic_selftest.h"
#include "sonar_component_selftest.h"

static void report(const char *name, bool passed)
{
    printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

int main(void)
{
    sonar_test_result_t (*const suites[])(sonar_test_report_fn) = {
        sonar_selftest_run, sonar_mic_selftest_run,
        sonar_speaker_selftest_run, sonar_codec_selftest_run, sonar_pdm_selftest_run,
        sonar_logic_selftest_run,
    };
    sonar_test_result_t total = {0U, 0U};
    for (size_t i = 0U; i < sizeof(suites) / sizeof(suites[0]); ++i) {
        sonar_test_result_t result = suites[i](report);
        total.passed += result.passed;
        total.failed += result.failed;
    }
    printf("%lu passed; %lu failed\n", (unsigned long)total.passed, (unsigned long)total.failed);
    return total.failed == 0U ? 0 : 1;
}
