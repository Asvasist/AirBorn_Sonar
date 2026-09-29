#include "sonar_console.h"
#include "sonar_stage2.h"
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

/* The input task forwards UART keys to the active experiment. */
#include "task.h"
#include "xuartps_hw.h"
#include "bspconfig.h"
#include <stddef.h>


#define SONAR_INPUT_STACK_WORDS 512U
static TaskHandle_t input_handle;
#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticTask_t input_task_storage;
static StackType_t input_task_stack[SONAR_INPUT_STACK_WORDS];
#endif

static void input_task(void *argument)
{
    (void)argument;
    for (;;) {
        for (unsigned i = 0U; i < 16U && XUartPs_IsReceiveData(STDIN_BASEADDRESS); ++i) {
            uint8_t key = XUartPs_RecvByte(STDIN_BASEADDRESS);
            if (key >= (uint8_t)'A' && key <= (uint8_t)'Z') { key = (uint8_t)(key - 'A' + 'a'); }
            sonar_stage2_key(key);
        }
        vTaskDelay(1U);
    }
}

bool sonar_console_input_create(void)
{
    if (input_handle != NULL) { return false; }
#if configSUPPORT_STATIC_ALLOCATION == 1
    input_handle = xTaskCreateStatic(input_task, "input", SONAR_INPUT_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, input_task_stack, &input_task_storage);
    return input_handle != NULL;
#else
    return xTaskCreate(input_task, "input", SONAR_INPUT_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, &input_handle) == pdPASS;
#endif
}

