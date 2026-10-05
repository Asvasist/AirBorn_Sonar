#include "sonar_rtos.h"
#include "sonar_sequencer.h"
#include "sonar_health.h"
#include "sonar_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "xparameters.h"
#include "xscuwdt.h"
#include "xstatus.h"
#include "xil_printf.h"
#include <stddef.h>

#if configUSE_PREEMPTION != 1
#error "Sonar requires preemptive FreeRTOS scheduling."
#endif
#if configMAX_PRIORITIES < 4
#error "Set configMAX_PRIORITIES to at least 4 in the Vitis BSP."
#endif
#if INCLUDE_vTaskDelay != 1 || INCLUDE_vTaskDelayUntil != 1
#error "Enable vTaskDelay and vTaskDelayUntil in the Vitis FreeRTOS BSP."
#endif
#if configTICK_RATE_HZ < 1 || configTICK_RATE_HZ > 1000
#error "Use a tick rate between 1 and 1000 Hz for the AMD xiltimer port."
#elif (1000 % configTICK_RATE_HZ) != 0
#error "The AMD xiltimer interval must be a whole number of milliseconds."
#endif

_Static_assert(sizeof(TickType_t) == sizeof(uint32_t), "Sonar requires 32-bit ticks.");
_Static_assert(SONAR_QUEUE_LENGTH > 0U, "Heartbeat queue must not be empty.");
_Static_assert(SONAR_TASK_STACK_WORDS >= 256U, "Task stack is too small for diagnostics.");

/* The private watchdog counts PERIPHCLK = CPU clock / 2. */
#define WATCHDOG_LOAD ((uint32_t)(XPAR_CPU_CORE_CLOCK_FREQ_HZ / 2U) * SONAR_WATCHDOG_S)
_Static_assert((uint64_t)(XPAR_CPU_CORE_CLOCK_FREQ_HZ / 2U) * SONAR_WATCHDOG_S <= UINT32_MAX,
               "Watchdog timeout exceeds the 32-bit counter.");
_Static_assert(SONAR_WATCHDOG_S * 1000U > 2U * SONAR_HEARTBEAT_MS,
               "Watchdog must outlast several supervisor periods.");

static QueueHandle_t heartbeat_queue;
static TaskHandle_t supervisor_handle;
static sonar_timing_t periods;
static XScuWdt watchdog;
/* Access through critical sections; this is a latch, not a dropped-event count. */
static bool queue_failed;
static bool creation_attempted;
/* Owned by the monitored task. */
static uint32_t sequence;
static uint32_t last_sent;

void sonar_rtos_heartbeat(void)
{
    uint32_t now = (uint32_t)xTaskGetTickCount();
    if (sequence != 0U && (uint32_t)(now - last_sent) < periods.heartbeat) { return; }
    sonar_heartbeat_t event = {sequence, now};
    if (xQueueSend(heartbeat_queue, &event, 0U) == pdPASS) {
        ++sequence;
        last_sent = now;
    } else {
        taskENTER_CRITICAL();
        queue_failed = true;
        taskEXIT_CRITICAL();
    }
}

static bool watchdog_start(void)
{
    XScuWdt_Config *config = XScuWdt_LookupConfig(XPAR_XSCUWDT_0_BASEADDR);
    if (config == NULL ||
        XScuWdt_CfgInitialize(&watchdog, config, config->BaseAddr) != XST_SUCCESS) {
        return false;
    }
    XScuWdt_SetWdMode(&watchdog);
    XScuWdt_LoadWdt(&watchdog, WATCHDOG_LOAD);
    XScuWdt_Start(&watchdog);
    return true;
}

/*
 * Two layers of supervision:
 *  - The experiment task, which owns the motor, speaker and DMA, must send a
 *    heartbeat at least every `periods.timeout` ticks. A stall latches a
 *    HEALTH FAULT: new work is inhibited, and the board keeps serving retained
 *    captures over Ethernet. The outputs are safe without that task: playback
 *    is a finite one-shot and the Tic stops itself when keep-alives stop
 *    (SONAR_MOTOR_WATCHDOG_MS).
 *  - This task restarts the Cortex-A9 private watchdog every period. If the
 *    scheduler, interrupts or this task stop, the watchdog resets the board
 *    after SONAR_WATCHDOG_S. sonar_halt() also ends in that reset.
 */
static void supervisor_task(void *argument)
{
    sonar_health_t health;
    sonar_heartbeat_t event;
    bool announced = false;
    uint32_t last_report;
    (void)argument;

    sonar_console_lock();
    xil_printf("SCHEDULER started; waiting for heartbeat\r\n");
    sonar_console_unlock();
    /* Monitoring starts with the first heartbeat, after peripheral initialization. */
    (void)xQueuePeek(heartbeat_queue, &event, portMAX_DELAY);
    if (!sonar_health_init(&health, event.tick, periods.timeout)) { sonar_halt("health init"); }
    if (!watchdog_start()) { sonar_halt("watchdog init"); }
    last_report = event.tick;

    for (;;) {
        bool overflow;
        BaseType_t received = xQueueReceive(heartbeat_queue, &event, (TickType_t)periods.heartbeat);
        uint32_t now = (uint32_t)xTaskGetTickCount();
        XScuWdt_RestartWdt(&watchdog);
        taskENTER_CRITICAL();
        overflow = queue_failed;
        taskEXIT_CRITICAL();
        if (overflow) { sonar_health_queue_fault(&health); }
        if (received == pdPASS) { (void)sonar_health_accept(&health, &event, now); }
        (void)sonar_health_poll(&health, now);

        if (health.state != SONAR_WAITING && health.state != SONAR_RUNNING) {
            sonar_sequencer_inhibit();
            sonar_console_lock();
            xil_printf("HEALTH FAULT %s; reset to restart\r\n", sonar_health_name(health.state));
            sonar_console_unlock();
            for (;;) {
                vTaskDelay((TickType_t)periods.heartbeat);
                XScuWdt_RestartWdt(&watchdog);
            }
        }
        if (health.state == SONAR_RUNNING &&
            (!announced || (uint32_t)(now - last_report) >= periods.report)) {
            sonar_console_lock();
            xil_printf("HEALTH RUNNING received=%u tick=%u\r\n",
                       (unsigned int)health.received, (unsigned int)now);
#if INCLUDE_uxTaskGetStackHighWaterMark == 1
            xil_printf("STACK free_min_words supervisor=%u\r\n",
                       (unsigned int)uxTaskGetStackHighWaterMark(supervisor_handle));
#endif
            sonar_console_unlock();
            last_report = now;
            announced = true;
        }
    }
}

bool sonar_rtos_create(const sonar_timing_t *timing)
{
    if (creation_attempted || timing == NULL || timing->heartbeat == 0U ||
        timing->timeout > UINT32_MAX / 2U || timing->report > UINT32_MAX / 2U ||
        (uint64_t)timing->timeout <= 2U * (uint64_t)timing->heartbeat ||
        timing->report < timing->heartbeat) { return false; }
    creation_attempted = true;
    periods = *timing;
    queue_failed = false;

    /* Allocated once at startup; no task allocates memory while running. */
    heartbeat_queue = xQueueCreate(SONAR_QUEUE_LENGTH, sizeof(sonar_heartbeat_t));
    if (heartbeat_queue == NULL) { return false; }
    return xTaskCreate(supervisor_task, "supervisor", SONAR_TASK_STACK_WORDS, NULL,
                       tskIDLE_PRIORITY + 3U, &supervisor_handle) == pdPASS;
}
