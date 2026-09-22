#include "sonar_mic_task.h"
#include "sonar_mic_config.h"
#include "sonar_mic_zynq.h"
#include "sonar_config.h"
#include "sonar_console.h"
#include "sonar_export.h"
#include "sonar_speaker_board.h"
#include "sonar_rtos.h"
#include "xil_printf.h"
#include "xuartps_hw.h"
#include "bspconfig.h"
#include <stddef.h>

#if SONAR_MIC_ENABLE_HARDWARE
_Static_assert(SONAR_MIC_CAPTURE_WORDS > 0U &&
    SONAR_MIC_CAPTURE_WORDS <= SONAR_MIC_MAX_DMA_BYTES / 4U, "Invalid capture size.");
_Static_assert(SONAR_MIC_PDM_HZ > 0U, "PDM clock must be nonzero.");
_Static_assert((uint64_t)SONAR_MIC_TIMEOUT_MS * SONAR_MIC_PDM_HZ >
    (uint64_t)SONAR_MIC_CAPTURE_WORDS * 32U * 1000U, "Timeout must exceed capture duration.");

/* Debugger-visible storage. Read only after MIC READY; never while DMA owns it. */
_Alignas(SONAR_MIC_CACHE_LINE) uint32_t sonar_mic_buffer[SONAR_MIC_BUFFER_BYTES / 4U];
static sonar_mic_t capture;
static sonar_export_t exporter;
static TaskHandle_t capture_handle;
static bool inhibited;

static bool write_export_packet(void *context, const uint8_t *packet, uint32_t bytes)
{
    (void)context;
    return sonar_console_write_packet(packet, bytes);
}

static void report_export(void)
{
    sonar_console_lock();
    if (exporter.state == EXPORT_DONE) {
        xil_printf("DOWNLOAD SENT bytes=%u; laptop verifies CRC before saving.\r\n",
                   (unsigned int)exporter.frame.bytes);
    } else if (exporter.state == EXPORT_FAILED || exporter.state == EXPORT_CANCELLED) {
        xil_printf("DOWNLOAD %s; retained capture can be downloaded again with d.\r\n",
                   exporter.state == EXPORT_FAILED ? "FAILED" : "CANCELLED");
    }
    sonar_console_unlock();
}
#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticTask_t task_storage;
static StackType_t task_stack[SONAR_MIC_STACK_WORDS];
#endif

static void report_state(void)
{
    sonar_mic_frame_t frame;
    sonar_console_lock();
    if (sonar_mic_frame(&capture, &frame)) {
        xil_printf("MIC READY bytes=%u generation=%u request_tick=%u completion_tick=%u\r\n",
            (unsigned int)frame.bytes, (unsigned int)frame.generation,
            (unsigned int)frame.request_tick, (unsigned int)frame.completion_tick);
        xil_printf("MIC raw_PDM first_word=0x%08x; transport complete, PL overflow unobservable\r\n",
                   (unsigned int)sonar_mic_buffer[0]);
    } else if (capture.state == MIC_FAULT) {
        xil_printf("MIC FAULT %s; DMA buffer %s; reset board before retry\r\n",
            sonar_mic_fault_name(capture.fault), capture.dma_owned ? "quarantined" : "stopped");
    } else {
        xil_printf("MIC %s\r\n", capture.state == MIC_CAPTURING ? "CAPTURING" : "IDLE");
    }
#if INCLUDE_uxTaskGetStackHighWaterMark == 1
    xil_printf("MIC STACK free_min_words=%u\r\n",
               (unsigned int)uxTaskGetStackHighWaterMark(capture_handle));
#endif
    sonar_console_unlock();
}

static void capture_task(void *argument)
{
    sonar_mic_io_t io;
    uint32_t timeout;
    uint32_t poll_ticks, retrigger_ticks, last_trigger = 0U;
    bool triggered = false;
    sonar_mic_state_t previous = MIC_IDLE;
    sonar_export_state_t previous_export = EXPORT_IDLE;
    sonar_mic_config_t config;
    (void)argument;
    const sonar_export_io_t export_io = {NULL, write_export_packet};
    if (!sonar_export_init(&exporter, &export_io)) { sonar_halt("capture export"); }
    if (!sonar_ms_to_ticks(SONAR_MIC_TIMEOUT_MS, (uint32_t)configTICK_RATE_HZ, &timeout) ||
        !sonar_ms_to_ticks(SONAR_MIC_POLL_MS, (uint32_t)configTICK_RATE_HZ, &poll_ticks) ||
        !sonar_ms_to_ticks(SONAR_MIC_RETRIGGER_MS, (uint32_t)configTICK_RATE_HZ, &retrigger_ticks)) {
        sonar_halt("microphone timing");
    }
    if (!sonar_mic_zynq_init(capture_handle, &io)) {
        sonar_console_lock();
        xil_printf("MIC INIT FAILED: check selected XSA/BSP and DMA reset; capture disabled\r\n");
        sonar_console_unlock();
        for (;;) { vTaskDelay((TickType_t)poll_ticks); }
    }
    config = (sonar_mic_config_t){SONAR_MIC_CAPTURE_BYTES, timeout, SONAR_MIC_PDM_HZ};
    if (!sonar_mic_init(&capture, &config, &io, (uint8_t *)sonar_mic_buffer,
                         (uint32_t)sizeof(sonar_mic_buffer))) { sonar_halt("microphone configuration"); }
    sonar_console_lock();
    xil_printf("MIC IDLE: r=capture, d=download, s=status, x=cancel. Capture also triggers speaker in this XSA.\r\n");
    sonar_console_unlock();

    for (;;) {
        sonar_mic_event_t event;
        bool stop_requested;
        taskENTER_CRITICAL();
        stop_requested = inhibited;
        taskEXIT_CRITICAL();
        if (stop_requested) {
            sonar_export_cancel(&exporter);
            sonar_mic_cancel(&capture);
            report_state();
            sonar_console_lock();
            xil_printf("MIC INHIBITED by system-health fault; reset board\r\n");
            sonar_console_unlock();
            for (;;) { vTaskDelay((TickType_t)poll_ticks); }
        }
        if (sonar_mic_zynq_take_event(&event)) {
            sonar_mic_event(&capture, &event, (uint32_t)xTaskGetTickCount());
        }
        sonar_mic_poll(&capture, (uint32_t)xTaskGetTickCount());
        uint8_t command;
        if (sonar_console_input_take(SONAR_INPUT_MIC, &command)) {
            if (command == (uint8_t)'r' || command == (uint8_t)'R') {
                uint32_t now = (uint32_t)xTaskGetTickCount();
                if (sonar_export_active(&exporter)) {
                    sonar_console_lock();
                    xil_printf("CAPTURE BLOCKED: download in progress; wait or cancel with x.\r\n");
                    sonar_console_unlock();
                } else if (!sonar_speaker_board_output_ready()) {
                    sonar_console_lock();
                    xil_printf("CAPTURE BLOCKED: press c, wait for CODEC READY, then u before r.\r\n");
                    sonar_console_unlock();
                } else if (triggered && (uint32_t)(now - last_trigger) < retrigger_ticks) {
                    sonar_console_lock();
                    xil_printf("CAPTURE BLOCKED: allow 2.1 s between triggers for the FPGA chirp sequence.\r\n");
                    sonar_console_unlock();
                } else {
                    if (capture.state == MIC_READY) { (void)sonar_mic_release(&capture); }
                    if (capture.state == MIC_IDLE) {
                        if (sonar_mic_start(&capture, now)) { last_trigger = now; triggered = true; }
                    } else { report_state(); }
                }
            } else if (command == (uint8_t)'x' || command == (uint8_t)'X') {
                sonar_export_cancel(&exporter);
                sonar_mic_cancel(&capture);
            } else if (command == (uint8_t)'d' || command == (uint8_t)'D') {
                sonar_mic_frame_t frame;
                if (!sonar_mic_frame(&capture, &frame) ||
                    !sonar_export_begin(&exporter, &frame, (uint32_t)configTICK_RATE_HZ)) {
                    sonar_console_lock();
                    xil_printf("DOWNLOAD BLOCKED: requires MIC READY and no active download.\r\n");
                    sonar_console_unlock();
                }
            } else if (command == (uint8_t)'s' || command == (uint8_t)'S') {
                report_state();
            }
        }
        if (capture.state != previous) { report_state(); previous = capture.state; }
        sonar_export_poll(&exporter);
        if (exporter.state != previous_export) {
            if (!sonar_export_active(&exporter)) { report_export(); }
            previous_export = exporter.state;
        }
        (void)ulTaskNotifyTake(pdTRUE, (TickType_t)poll_ticks);
    }
}

#endif

void sonar_mic_task_inhibit(void)
{
#if SONAR_MIC_ENABLE_HARDWARE
    taskENTER_CRITICAL();
    inhibited = true;
    taskEXIT_CRITICAL();
#endif
}
bool sonar_mic_task_create(void)
{
#if !SONAR_MIC_ENABLE_HARDWARE
    return true;
#else
    if (capture_handle != NULL) { return false; }
#if configSUPPORT_STATIC_ALLOCATION == 1
    capture_handle = xTaskCreateStatic(capture_task, "mic", SONAR_MIC_STACK_WORDS,
        NULL, tskIDLE_PRIORITY + 3U, task_stack, &task_storage);
    return capture_handle != NULL;
#else
    return xTaskCreate(capture_task, "mic", SONAR_MIC_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 3U, &capture_handle) == pdPASS;
#endif
#endif
}
