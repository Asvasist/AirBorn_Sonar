#include "sonar_rtos.h"
#include "sonar_health.h"
#include "sonar_console.h"
#include "sonar_mic_task.h"
#include "sonar_motor_task.h"
#include "sonar_scan_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
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

static QueueHandle_t heartbeat_queue;
static TaskHandle_t producer_handle;
static TaskHandle_t supervisor_handle;
static sonar_timing_t periods;
/* Access through critical sections; this is a latch, not a dropped-event count. */
static bool queue_failed;
static bool creation_attempted;

#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticQueue_t queue_storage;
static uint8_t queue_bytes[SONAR_QUEUE_LENGTH * sizeof(sonar_heartbeat_t)];
static StaticTask_t producer_storage;
static StaticTask_t supervisor_storage;
static StackType_t producer_stack[SONAR_TASK_STACK_WORDS];
static StackType_t supervisor_stack[SONAR_TASK_STACK_WORDS];
#elif configSUPPORT_DYNAMIC_ALLOCATION != 1
#error "Enable static or dynamic allocation in the Vitis FreeRTOS BSP."
#endif

static void producer_task(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t sequence = 0U;
    (void)argument;
    for (;;) {
        sonar_heartbeat_t event = {sequence, (uint32_t)xTaskGetTickCount()};
        if (xQueueSend(heartbeat_queue, &event, 0U) != pdPASS) {
            taskENTER_CRITICAL();
            queue_failed = true;
            taskEXIT_CRITICAL();
            /* The supervisor owns fault reporting; this task stays blocked. */
            for (;;) { vTaskDelay((TickType_t)periods.heartbeat); }
        }
        ++sequence;
        vTaskDelayUntil(&wake, (TickType_t)periods.heartbeat);
    }
}

static void supervisor_task(void *argument)
{
    sonar_health_t health;
    uint32_t now = (uint32_t)xTaskGetTickCount();
    uint32_t last_report = now;
    bool announced = false;
    (void)argument;
    if (!sonar_health_init(&health, now, periods.timeout)) { sonar_halt("health init"); }
    sonar_console_lock();
    xil_printf("SCHEDULER started; waiting for heartbeat\r\n");
    sonar_console_unlock();

    for (;;) {
        sonar_heartbeat_t event;
        bool overflow;
        BaseType_t received = xQueueReceive(heartbeat_queue, &event,
                                            (TickType_t)periods.heartbeat);
        now = (uint32_t)xTaskGetTickCount();
        taskENTER_CRITICAL();
        overflow = queue_failed;
        taskEXIT_CRITICAL();
        if (overflow) { sonar_health_queue_fault(&health); }
        if (received == pdPASS) { (void)sonar_health_accept(&health, &event, now); }
        (void)sonar_health_poll(&health, now);

        if (health.state != SONAR_WAITING && health.state != SONAR_RUNNING) {
            sonar_mic_task_inhibit();
            sonar_motor_task_inhibit();
            sonar_scan_task_inhibit();
            sonar_console_lock();
            xil_printf("HEALTH FAULT %s; reset to restart\r\n", sonar_health_name(health.state));
            sonar_console_unlock();
            for (;;) { vTaskDelay((TickType_t)periods.report); }
        }
        if (health.state == SONAR_RUNNING &&
            (!announced || (uint32_t)(now - last_report) >= periods.report)) {
            sonar_console_lock();
            xil_printf("HEALTH RUNNING received=%u tick=%u\r\n",
                       (unsigned int)health.received, (unsigned int)now);
#if INCLUDE_uxTaskGetStackHighWaterMark == 1
            xil_printf("STACK free_min_words producer=%u supervisor=%u\r\n",
                       (unsigned int)uxTaskGetStackHighWaterMark(producer_handle),
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

#if configSUPPORT_STATIC_ALLOCATION == 1
    heartbeat_queue = xQueueCreateStatic(SONAR_QUEUE_LENGTH, sizeof(sonar_heartbeat_t),
                                        queue_bytes, &queue_storage);
    if (heartbeat_queue == NULL) { return false; }
    producer_handle = xTaskCreateStatic(producer_task, "heartbeat", SONAR_TASK_STACK_WORDS,
        NULL, tskIDLE_PRIORITY + 1U, producer_stack, &producer_storage);
    if (producer_handle == NULL) { return false; }
    supervisor_handle = xTaskCreateStatic(supervisor_task, "supervisor", SONAR_TASK_STACK_WORDS,
        NULL, tskIDLE_PRIORITY + 2U, supervisor_stack, &supervisor_storage);
    return supervisor_handle != NULL;
#else
    /* Allocate once at startup. Neither task allocates memory while running. */
    heartbeat_queue = xQueueCreate(SONAR_QUEUE_LENGTH, sizeof(sonar_heartbeat_t));
    if (heartbeat_queue == NULL) { return false; }
    if (xTaskCreate(producer_task, "heartbeat", SONAR_TASK_STACK_WORDS, NULL,
                    tskIDLE_PRIORITY + 1U, &producer_handle) != pdPASS) { return false; }
    return xTaskCreate(supervisor_task, "supervisor", SONAR_TASK_STACK_WORDS, NULL,
                       tskIDLE_PRIORITY + 2U, &supervisor_handle) == pdPASS;
#endif
}
