# Host unit tests for the hardware-independent firmware modules.
#
#   cmake -S firmware/tests -B build/host-tests
#   cmake --build build/host-tests
#   ctest --test-dir build/host-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(sonar_host_tests LANGUAGES C)

set(FW_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_executable(sonar_tests
    test_main.c
    sonar_selftest.c sonar_mic_selftest.c sonar_speaker_selftest.c sonar_codec_selftest.c
    test_pdm.c test_logic.c
    ${FW_SRC}/sonar_config.c ${FW_SRC}/sonar_health.c ${FW_SRC}/sonar_bsp_validation.c
    ${FW_SRC}/sonar_mic.c ${FW_SRC}/sonar_codec_service.c ${FW_SRC}/sonar_codec_bus.c
    ${FW_SRC}/sonar_pdm_decimator.c ${FW_SRC}/sonar_wav_writer.c ${FW_SRC}/sonar_measurement_cycle.c ${FW_SRC}/sonar_frames.c
    ${FW_SRC}/sonar_console_buffer.c ${FW_SRC}/sonar_control_protocol.c ${FW_SRC}/sonar_integer_parser.c
    ${FW_SRC}/sonar_scan360.c ${FW_SRC}/sonar_experiment_config.c ${FW_SRC}/sonar_identity.c)
target_include_directories(sonar_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FW_SRC})
set_target_properties(sonar_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(sonar_tests PRIVATE SONAR_PDM_TEST_REFERENCE=1)
target_compile_options(sonar_tests PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow)

enable_testing()
add_test(NAME sonar_tests COMMAND sonar_tests)
