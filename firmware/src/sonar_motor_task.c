#include "sonar_motor_task.h"
#include "sonar_motor_zynq.h"
#include "sonar_motor_config.h"
#include "sonar_speaker_board.h"
#include "sonar_config.h"
#include "sonar_console.h"
#include "sonar_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xuartps_hw.h"
#include "bspconfig.h"
#include <stddef.h>

#if SONAR_MOTOR_ENABLE_HARDWARE || SONAR_SPEAKER_ENABLE_HARDWARE
static sonar_tic_t tic;
static bool motor_available;
static TaskHandle_t motor_handle;
static bool inhibited;
static const char *const codec_states[] = {"OFF", "CONFIGURING", "READY", "FAULT"};
#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticTask_t task_storage;
static StackType_t task_stack[SONAR_MOTOR_STACK_WORDS];
#endif
static void report(void)
{
    static const char *const states[] = {"IDLE", "RUNNING", "DONE", "FAULT"};
    sonar_console_lock();
    if (motor_available) {
    xil_printf("MOTOR %s fault=%u controller_stop=%s\r\n", states[tic.motor.state],
        (unsigned int)tic.motor.fault, tic.motor.stop_confirmed ? "confirmed" : "unconfirmed");
    if (tic.status_valid) {
        xil_printf("TIC state=%u errors=0x%x flags=0x%x commanded_position=%d commanded_velocity=%d\r\n",
            (unsigned int)tic.status.operation_state, (unsigned int)tic.status.errors,
            (unsigned int)tic.status.flags, (int)tic.status.position, (int)tic.status.velocity);
    } else { xil_printf("TIC status unavailable; physical motion unverified\r\n"); }
    if (tic.motor.state == MOTOR_FAULT) { xil_printf("MOTOR fault latched; reset before another movement\r\n"); }
    } else { xil_printf("MOTOR unavailable; check PS setup or hardware-enable configuration\r\n"); }
    const sonar_speaker_t *codec = sonar_speaker_board_state();
    if (codec != NULL) {
        xil_printf("CODEC %s fault=%u enabled=%u mute_confirmed=%u bus_owned=%u\r\n",
            codec_states[codec->state], (unsigned int)codec->fault,
            (unsigned int)codec->output_enabled, (unsigned int)codec->mute_confirmed,
            (unsigned int)codec->bus_owned);
    } else { xil_printf("CODEC unavailable: initialization failed or hardware disabled\r\n"); }
    sonar_console_unlock();
}
static bool make_config(sonar_tic_config_t *config, uint32_t *poll)
{
    *config = (sonar_tic_config_t){.address = SONAR_MOTOR_ADDRESS,
        .max_speed = SONAR_MOTOR_SPEED, .acceleration = SONAR_MOTOR_ACCELERATION,
        .deceleration = SONAR_MOTOR_DECELERATION};
    return sonar_ms_to_ticks(SONAR_MOTOR_RUN_MS, (uint32_t)configTICK_RATE_HZ, &config->run_ticks) &&
        sonar_ms_to_ticks(SONAR_MOTOR_OPERATION_TIMEOUT_MS, (uint32_t)configTICK_RATE_HZ, &config->operation_timeout_ticks) &&
        sonar_ms_to_ticks(SONAR_MOTOR_KEEPALIVE_MS, (uint32_t)configTICK_RATE_HZ, &config->keepalive_ticks) &&
        sonar_ms_to_ticks(SONAR_MOTOR_WATCHDOG_MS, (uint32_t)configTICK_RATE_HZ, &config->watchdog_ticks) &&
        sonar_ms_to_ticks(SONAR_MOTOR_POLL_MS, (uint32_t)configTICK_RATE_HZ, poll);
}
static void motor_task(void *argument)
{
    sonar_tic_io_t io;
    sonar_tic_config_t config;
    uint32_t poll;
    bool stopped_by_health = false;
    (void)argument;
    if (!make_config(&config, &poll)) { sonar_halt("motor timing configuration"); }
    motor_available = sonar_motor_zynq_init(&io);
    if (!motor_available) {
        sonar_console_lock();
        xil_printf("MOTOR INIT FAILED: PS I2C1 clock/reset, MIO12/13 routing or bus state; no motor commands sent\r\n");
        sonar_console_unlock();
    }
    if (motor_available && !sonar_tic_init(&tic, &config, &io)) { sonar_halt("Tic configuration"); }
    if (!sonar_speaker_board_init()) {
        sonar_console_lock(); xil_printf("CODEC INIT FAILED: check BSP, AXI IIC and GPIO readback\r\n"); sonar_console_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(1000U));
    if (motor_available) { (void)sonar_tic_status(&tic); } /* Never energize at startup. */
    sonar_console_lock();
    xil_printf("MOTOR: f=forward, b=reverse (backward), x=halt/de-energize + cancel MIC, s=status; no automatic motion\r\n");
    xil_printf("MOTOR run_ms=%u velocity_units=%u\r\n",
        (unsigned int)SONAR_MOTOR_RUN_MS, (unsigned int)SONAR_MOTOR_SPEED);
    xil_printf("TIC velocity/position are commanded steps, not measured shaft feedback\r\n");
    xil_printf("CODEC: c=configure; wait for READY, then u=unmute; r=record+play; m=mute\r\n");
    sonar_console_unlock();
    report();
    for (;;) {
        bool stop_requested;
        sonar_motor_state_t previous = tic.motor.state;
        const sonar_speaker_t *codec_before = sonar_speaker_board_state();
        sonar_speaker_state_t previous_codec = codec_before != NULL ? codec_before->state : SPEAKER_OFF;
        taskENTER_CRITICAL(); stop_requested = inhibited; taskEXIT_CRITICAL();
        if (stop_requested && !stopped_by_health) {
            stopped_by_health = true;
            if (motor_available) { sonar_tic_cancel(&tic); }
            sonar_speaker_board_cancel();
            sonar_console_lock(); xil_printf("MOTOR inhibited by system-health fault\r\n"); sonar_console_unlock();
        }
        if (!stop_requested) {
            if (motor_available) { sonar_tic_poll(&tic); }
            sonar_speaker_board_poll((uint32_t)xTaskGetTickCount());
        }
        uint8_t command;
        if (sonar_console_input_take(SONAR_INPUT_MOTOR, &command)) {
            /* 'r' belongs to the microphone capture; reverse is now 'b'. */
            if ((command == (uint8_t)'f' || command == (uint8_t)'b') && !stop_requested && motor_available) {
                int32_t velocity = command == (uint8_t)'f' ? (int32_t)SONAR_MOTOR_SPEED : -(int32_t)SONAR_MOTOR_SPEED;
                if (!sonar_tic_start(&tic, velocity)) { report(); }
            } else if (command == (uint8_t)'x') {
                if (motor_available) { sonar_tic_cancel(&tic); }
                sonar_speaker_board_cancel();
            } else if (command == (uint8_t)'c' && !stop_requested) {
                (void)sonar_speaker_board_begin((uint32_t)xTaskGetTickCount()); report();
            } else if (command == (uint8_t)'u' && !stop_requested) {
                (void)sonar_speaker_board_enable(true); report();
            } else if (command == (uint8_t)'m') {
                (void)sonar_speaker_board_enable(false); report();
            }
            else if (command == (uint8_t)'s') {
                /* The poll already refreshed running status; avoid delaying keepalive. */
                if (motor_available && tic.motor.state != MOTOR_RUNNING) { (void)sonar_tic_status(&tic); }
                report();
            }
        }
        const sonar_speaker_t *codec_after = sonar_speaker_board_state();
        if (tic.motor.state != previous || (codec_after != NULL && codec_after->state != previous_codec)) { report(); }
        vTaskDelay((TickType_t)poll);
    }
}
#endif
void sonar_motor_task_inhibit(void)
{
#if SONAR_MOTOR_ENABLE_HARDWARE || SONAR_SPEAKER_ENABLE_HARDWARE
    taskENTER_CRITICAL(); inhibited = true; taskEXIT_CRITICAL();
#endif
}
bool sonar_motor_task_create(void)
{
#if SONAR_MOTOR_ENABLE_HARDWARE || SONAR_SPEAKER_ENABLE_HARDWARE
    if (motor_handle != NULL) { return false; }
#if configSUPPORT_STATIC_ALLOCATION == 1
    motor_handle = xTaskCreateStatic(motor_task, "motor", SONAR_MOTOR_STACK_WORDS,
        NULL, tskIDLE_PRIORITY + 3U, task_stack, &task_storage);
    return motor_handle != NULL;
#else
    return xTaskCreate(motor_task, "motor", SONAR_MOTOR_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 3U, &motor_handle) == pdPASS;
#endif
#else
    return true;
#endif
}
