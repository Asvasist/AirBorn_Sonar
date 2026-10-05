#include "sonar_pl_gpio.h"
#include "sonar_profile.h"
#include "sonar_codec_bus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
static bool attempted, healthy;
static uint32_t shadow;
bool sonar_pl_gpio_init(void)
{
    bool result;
    taskENTER_CRITICAL();
    if (!attempted) {
        attempted = true;
        Xil_Out32(SONAR_EXPECTED_GPIO_BASE, 0U);
        Xil_Out32(SONAR_EXPECTED_GPIO_BASE + 4U, 0U);
        SYNCHRONIZE_IO;
        healthy = (Xil_In32(SONAR_EXPECTED_GPIO_BASE) & 3U) == 0U;
        shadow = 0U;
    }
    result = healthy;
    taskEXIT_CRITICAL();
    return result;
}
bool sonar_pl_gpio_update(uint32_t mask, uint32_t value)
{
    uint32_t next;
    bool result = false;
    taskENTER_CRITICAL();
    if (healthy && sonar_gpio_merge(shadow, mask, value, &next)) {
        Xil_Out32(SONAR_EXPECTED_GPIO_BASE, next);
        SYNCHRONIZE_IO;
        healthy = (Xil_In32(SONAR_EXPECTED_GPIO_BASE) & 3U) == next;
        if (healthy) { shadow = next; result = true; }
    }
    taskEXIT_CRITICAL();
    return result;
}
