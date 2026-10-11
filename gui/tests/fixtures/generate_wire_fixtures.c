/* Build from the repository root, using the actual firmware encoders:
 * cc -std=c11 -I firmware/src gui/tests/fixtures/generate_wire_fixtures.c
 *    firmware/src/sonar_frames.c firmware/src/sonar_control_protocol.c
 *    -o build/generate_wire_fixtures
 * build/generate_wire_fixtures gui/tests/fixtures
 */
#include "sonar_control_protocol.h"
#include "sonar_frames.h"
#include <stdio.h>

static int save(const char *directory, const char *name, const uint8_t *data, size_t bytes)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return 1;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return 1;
    }
    size_t written = fwrite(data, 1U, bytes, file);
    int close_result = fclose(file);
    return written != bytes || close_result != 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 1;
    }
    const sonar_frame_meta_t meta = {
        .sequence = 7U,
        .flags = (7U << 16U) | 15U,
        .trigger_us = UINT64_C(1234567890123),
        .completion_us = UINT64_C(1234567940123),
        .position = -16,
        .steps = -8,
        .divisor = 1U,
        .tx_duration_us = 35000U,
        .boot_id = {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U},
        .test_id = {0x55555555U, 0x66666666U, 0x77777777U, 0x88888888U},
    };
    const uint8_t payload[4] = {1U, 128U, 2U, 64U};
    uint8_t frame[SONAR_WIRE_HEADER];
    sonar_frame_header(frame, &meta, sizeof(payload), sonar_crc32(payload, sizeof(payload)));
    if (save(argv[1], "wire_v2.bin", frame, sizeof(frame))) {
        return 1;
    }
    const uint32_t words[SONAR_CONTROL_REPLY_WORDS] = {
        0U, 3U, 1U, 7U, 0U, 2500U, 5600U, 35000U, 40U, 0U,
        3360U, 0U, 0U, 0U, 96000U, 4704U, 50000U, 1000U, 0U, 0U, 0U, 46000U,
        0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U,
        0x55555555U, 0x66666666U, 0x77777777U, 0x88888888U,
    };
    uint8_t response[SONAR_CONTROL_HEADER + SONAR_CONTROL_REPLY_BYTES];
    for (unsigned i = 0U; i < SONAR_CONTROL_REPLY_WORDS; ++i) {
        sonar_put_u32(response + SONAR_CONTROL_HEADER + i * 4U, words[i]);
    }
    sonar_control_header(response, CTRL_GET_CONFIGURATION | SONAR_CONTROL_RESPONSE, 1U,
                         response + SONAR_CONTROL_HEADER, SONAR_CONTROL_REPLY_BYTES);
    return save(argv[1], "control_v2.bin", response, sizeof(response));
}
