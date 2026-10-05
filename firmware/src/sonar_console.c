#include "sonar_console.h"
#include "sonar_sequencer.h"
#include "sonar_rtos.h"
#include "FreeRTOS.h"
#include "semphr.h"

#if configUSE_MUTEXES != 1
#error "Enable FreeRTOS mutex support for task console output."
#endif
static SemaphoreHandle_t console_mutex;

bool sonar_console_create(void)
{
    if (console_mutex != NULL) { return false; }
    console_mutex = xSemaphoreCreateMutex();
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
