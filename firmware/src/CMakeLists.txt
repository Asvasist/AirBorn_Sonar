# Vitis 2025.2 application build, following AMD's FreeRTOS template layout.
cmake_minimum_required(VERSION 3.16)

# These files describe the application's generated platform and memory layout.
# Keep them when copying this payload into the Vitis application src directory.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Freertos_hello_worldExample.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/Freertos_hello_worldExample.cmake")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Empty_applicationExample.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/Empty_applicationExample.cmake")
else()
    message(FATAL_ERROR "Create the Vitis application component first (see firmware/README.md); it generates the platform include file.")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/UserConfig.cmake")

get_filename_component(_component_dir "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(APP_NAME "${_component_dir}" NAME)
project(${APP_NAME} LANGUAGES C ASM)
find_package(common REQUIRED)

collect(PROJECT_LIB_DEPS xilstandalone;xiltimer;lwip220;freertos)
collect(PROJECT_LIB_DEPS freertos;lwip220)
collect(PROJECT_LIB_DEPS xil)
collect(PROJECT_LIB_DEPS xiltimer)
collect(PROJECT_LIB_DEPS gcc)
collect(PROJECT_LIB_DEPS c)
collector_list(_deps PROJECT_LIB_DEPS)
list(APPEND _deps ${USER_LINK_LIBRARIES})

# Explicit sources prevent a retained template main() from entering the build.
set(_sources main.c sonar_config.c sonar_health.c sonar_profile.c sonar_platform.c
    sonar_rtos.c sonar_fault.c sonar_console.c sonar_console_buffer.c sonar_tcp_console.c
    sonar_stage2.c sonar_cycle.c sonar_frames.c sonar_number.c sonar_scan360.c
    sonar_mic.c sonar_mic_zynq.c sonar_pdm.c sonar_wav.c sonar_processing.c sonar_network.c
    sonar_stepper.c sonar_ps_i2c.c sonar_motor_zynq.c
    sonar_speaker.c sonar_speaker_board.c sonar_codec_bus.c sonar_pl_gpio.c
    sonar_chirp.c sonar_waveform.c sonar_tx_bram.c sonar_wav_player.c
    sonar_experiment_config.c sonar_experiment.c
    sonar_control_protocol.c sonar_control_server.c sonar_socket.c)
# Use the application's existing memory layout; generated templates are optional.
if(NOT USER_LINKER_SCRIPT)
    set(USER_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/lscript.ld")
endif()
get_filename_component(USER_LINKER_SCRIPT "${USER_LINKER_SCRIPT}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${USER_LINKER_SCRIPT}")
    message(FATAL_ERROR "The application linker script is missing: ${USER_LINKER_SCRIPT}")
endif()
string(APPEND CMAKE_C_FLAGS ${USER_COMPILE_OPTIONS})
string(APPEND CMAKE_C_LINK_FLAGS ${USER_LINK_OPTIONS})
add_dependency_on_bsp(_sources)
add_executable(${APP_NAME}.elf ${_sources})
set_target_properties(${APP_NAME}.elf PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
    LINK_DEPENDS "${USER_LINKER_SCRIPT}")
target_compile_options(${APP_NAME}.elf PRIVATE -Wall -Wextra -Werror)
# Redirect BSP stdout/stderr into the buffered Ethernet console.
target_link_options(${APP_NAME}.elf PRIVATE "-Wl,--wrap=outbyte")
target_compile_definitions(${APP_NAME}.elf PRIVATE ${USER_COMPILE_DEFINITIONS})
target_include_directories(${APP_NAME}.elf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" ${USER_INCLUDE_DIRECTORIES})

string(REPLACE ";" ",-l" _deps "${_deps}")
set(_linker_command "-Wl,-T -Wl,\"${USER_LINKER_SCRIPT}\" -L\"${CMAKE_SOURCE_DIR}/\" -L\"${CMAKE_LIBRARY_PATH}/\" -L\"${USER_LINK_DIRECTORIES}/\" -Wl,--start-group,-l${_deps} -Wl,--end-group")
target_link_libraries(${APP_NAME}.elf ${_linker_command})
print_elf_size(CMAKE_SIZE ${APP_NAME})
