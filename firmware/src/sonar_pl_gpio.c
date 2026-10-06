#include "sonar_pl_gpio.h"
#include "sonar_profile.h"
#include "sonar_codec_bus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"

static bool initialized;
static uint32_t shadow;

bool sonar_pl_gpio_init(void)
{
    taskENTER_CRITICAL();
    if (!initialized) {
        shadow = 0U;
        Xil_Out32(SONAR_EXPECTED_GPIO_BASE, shadow);
        Xil_Out32(SONAR_EXPECTED_GPIO_BASE + 4U, 0U);
        SYNCHRONIZE_IO;
        initialized = true;
    }
    taskEXIT_CRITICAL();
    return true;
}

bool sonar_pl_gpio_update(uint32_t mask, uint32_t value)
{
    uint32_t next;
    bool result = false;
    taskENTER_CRITICAL();
    if (initialized && sonar_gpio_merge(shadow, mask, value, &next)) {
        Xil_Out32(SONAR_EXPECTED_GPIO_BASE, next);
        SYNCHRONIZE_IO;
        /* Output DATA reads cannot verify writes; retain the software shadow. */
        shadow = next;
        result = true;
    }
    taskEXIT_CRITICAL();
    return result;
}
