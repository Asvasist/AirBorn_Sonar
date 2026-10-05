#include "sonar_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"

/* Available in the debugger even if UART output is unavailable. */
const char * volatile sonar_fault_reason;
volatile uint32_t sonar_assert_line;
volatile TaskHandle_t sonar_overflow_task;

void sonar_halt(const char *reason)
{
    sonar_fault_reason = reason;
    portDISABLE_INTERRUPTS();
    xil_printf("HALT %s\r\n", reason);
    for (;;) { __asm__ volatile ("nop"); }
}

/* These replace the weak hooks in AMD's Cortex-A9 FreeRTOS port. */
void vApplicationMallocFailedHook(void)
{
    sonar_halt("FreeRTOS allocation failed");
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)name;
    sonar_overflow_task = task;
    sonar_fault_reason = "task stack overflow";
    portDISABLE_INTERRUPTS();
    /* Avoid formatting or dereferencing the task name on a damaged stack. */
    for (;;) { __asm__ volatile ("nop"); }
}

void vApplicationAssert(const char *file, uint32_t line)
{
    sonar_assert_line = line;
    sonar_halt(file);
}
