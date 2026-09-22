#ifndef SONAR_PROFILE_H
#define SONAR_PROFILE_H

#include <stdint.h>

/* AirbornSonar_Initialtest.xsa; declarations alone cannot identify a loaded bitstream. */
#define SONAR_EXPECTED_GPIO_BASE UINT32_C(0x41200000)
#define SONAR_EXPECTED_GPIO_WIDTH 2U
#define SONAR_EXPECTED_DMA_BASE  UINT32_C(0x40400000)
#define SONAR_EXPECTED_IIC_BASE  UINT32_C(0x41600000)
#define SONAR_PROFILE_GPIO UINT32_C(1)
#define SONAR_PROFILE_DMA  UINT32_C(2)
#define SONAR_PROFILE_IIC  UINT32_C(4)

typedef struct {
    uint32_t gpio_base;
    uint32_t gpio_width;
    uint32_t dma_base;
    uint32_t dma_has_s2mm;
    uint32_t iic_base;
} sonar_profile_t;

/* Return a bitmask of missing or different BSP declarations. No peripheral I/O. */
uint32_t sonar_profile_check(const sonar_profile_t *profile);

#endif
