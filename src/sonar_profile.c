#include "sonar_profile.h"
#include <stddef.h>

uint32_t sonar_profile_check(const sonar_profile_t *profile)
{
    uint32_t mismatch = 0U;
    if (profile == NULL) { return SONAR_PROFILE_GPIO | SONAR_PROFILE_DMA | SONAR_PROFILE_IIC; }
    if (profile->gpio_base != SONAR_EXPECTED_GPIO_BASE || profile->gpio_width != SONAR_EXPECTED_GPIO_WIDTH) {
        mismatch |= SONAR_PROFILE_GPIO;
    }
    if (profile->dma_base != SONAR_EXPECTED_DMA_BASE || profile->dma_has_s2mm != 1U) {
        mismatch |= SONAR_PROFILE_DMA;
    }
    if (profile->iic_base != SONAR_EXPECTED_IIC_BASE) { mismatch |= SONAR_PROFILE_IIC; }
    return mismatch;
}
