#include "sonar_console.h"
#include "sonar_rtos.h"
#include "FreeRTOS.h"
#include "semphr.h"

#if configUSE_MUTEXES != 1
#error "Enable FreeRTOS mutex support for task console output."
#endif
static SemaphoreHandle_t console_mutex;
#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticSemaphore_t console_storage;
#endif

bool sonar_console_create(void)
{
    if (console_mutex != NULL) { return false; }
#if configSUPPORT_STATIC_ALLOCATION == 1
    console_mutex = xSemaphoreCreateMutexStatic(&console_storage);
#else
    console_mutex = xSemaphoreCreateMutex();
#endif
    return console_mutex != NULL;
}
void sonar_console_lock(void)
{
    if (xSemaphoreTake(console_mutex, portMAX_DELAY) != pdTRUE) { sonar_halt("console mutex"); }
}
void sonar_console_unlock(void)
{
    if (xSemaphoreGive(console_mutex) != pdTRUE) { sonar_halt("console mutex release"); }
}

/* ---------------- Keyboard input router ---------------- */
#include "task.h"
#include "queue.h"
#include "xuartps_hw.h"
#include "bspconfig.h"
#include <stddef.h>
#include "sonar_export.h"

bool sonar_console_write_packet(const uint8_t *data, uint32_t bytes)
{
    TickType_t limit = pdMS_TO_TICKS(100U);
    TickType_t started;
    bool sent = true;
    if (limit == 0U) { limit = 1U; }
    if (console_mutex == NULL || data == NULL || bytes == 0U ||
        bytes > SONAR_EXPORT_PACKET_MAX) { return false; }
    if (xSemaphoreTake(console_mutex, limit) != pdTRUE) { return false; }
    started = xTaskGetTickCount();
    for (uint32_t i = 0U; i < bytes; ++i) {
        while ((XUartPs_ReadReg(STDOUT_BASEADDRESS, XUARTPS_SR_OFFSET) &
                XUARTPS_SR_TXFULL) != 0U) {
            if ((TickType_t)(xTaskGetTickCount() - started) >= limit) {
                sent = false;
                break;
            }
            vTaskDelay(1U);
        }
        if (!sent) { break; }
        XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_FIFO_OFFSET, data[i]);
    }
    sonar_console_unlock();
    return sent;
}

#define SONAR_INPUT_QUEUE_LENGTH 8U
#define SONAR_INPUT_STACK_WORDS 512U
static QueueHandle_t input_queue[2];
static TaskHandle_t input_handle;
#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticQueue_t input_queue_storage[2];
static uint8_t input_queue_bytes[2][SONAR_INPUT_QUEUE_LENGTH];
static StaticTask_t input_task_storage;
static StackType_t input_task_stack[SONAR_INPUT_STACK_WORDS];
#endif

static void route(sonar_input_target_t target, uint8_t key)
{
    /* Never block the reader; a full queue means that consumer is busy. */
    (void)xQueueSend(input_queue[target], &key, 0U);
}

static void input_task(void *argument)
{
    (void)argument;
    for (;;) {
        for (unsigned i = 0U; i < 16U && XUartPs_IsReceiveData(STDIN_BASEADDRESS); ++i) {
            uint8_t key = XUartPs_RecvByte(STDIN_BASEADDRESS);
            if (key >= (uint8_t)'A' && key <= (uint8_t)'Z') { key = (uint8_t)(key - 'A' + 'a'); }
            switch (key) {
            case 'r': case 'd': route(SONAR_INPUT_MIC, key); break;
            case 's': case 'x': route(SONAR_INPUT_MIC, key); route(SONAR_INPUT_MOTOR, key); break;
            case 'f': case 'b': case 'c': case 'u': case 'm': route(SONAR_INPUT_MOTOR, key); break;
            default: break;
            }
        }
        vTaskDelay(1U);
    }
}

bool sonar_console_input_create(void)
{
    if (input_handle != NULL) { return false; }
    for (unsigned i = 0U; i < 2U; ++i) {
#if configSUPPORT_STATIC_ALLOCATION == 1
        input_queue[i] = xQueueCreateStatic(SONAR_INPUT_QUEUE_LENGTH, 1U,
                                            input_queue_bytes[i], &input_queue_storage[i]);
#else
        input_queue[i] = xQueueCreate(SONAR_INPUT_QUEUE_LENGTH, 1U);
#endif
        if (input_queue[i] == NULL) { return false; }
    }
#if configSUPPORT_STATIC_ALLOCATION == 1
    input_handle = xTaskCreateStatic(input_task, "input", SONAR_INPUT_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, input_task_stack, &input_task_storage);
    return input_handle != NULL;
#else
    return xTaskCreate(input_task, "input", SONAR_INPUT_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, &input_handle) == pdPASS;
#endif
}

bool sonar_console_input_take(sonar_input_target_t target, uint8_t *key)
{
    if (key == NULL || (unsigned)target > 1U || input_queue[target] == NULL) { return false; }
    return xQueueReceive(input_queue[target], key, 0U) == pdPASS;
}
