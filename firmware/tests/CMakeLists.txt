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
    ${FW_SRC}/sonar_config.c ${FW_SRC}/sonar_health.c ${FW_SRC}/sonar_profile.c
    ${FW_SRC}/sonar_mic.c ${FW_SRC}/sonar_speaker.c ${FW_SRC}/sonar_codec_bus.c
    ${FW_SRC}/sonar_pdm.c ${FW_SRC}/sonar_wav.c ${FW_SRC}/sonar_cycle.c ${FW_SRC}/sonar_frames.c
    ${FW_SRC}/sonar_console_buffer.c ${FW_SRC}/sonar_control_protocol.c ${FW_SRC}/sonar_number.c
    ${FW_SRC}/sonar_scan360.c ${FW_SRC}/sonar_experiment_config.c)
target_include_directories(sonar_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FW_SRC})
set_target_properties(sonar_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_options(sonar_tests PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow)

enable_testing()
add_test(NAME sonar_tests COMMAND sonar_tests)
