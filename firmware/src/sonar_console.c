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
