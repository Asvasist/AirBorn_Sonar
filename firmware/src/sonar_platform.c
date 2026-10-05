#include "sonar_platform.h"
#include "xparameters.h"

sonar_profile_t sonar_platform_profile(void)
{
    sonar_profile_t profile = {0U, 0U, 0U, 0U, 0U};
#if defined(XPAR_AXI_GPIO_0_BASEADDR)
    profile.gpio_base = (uint32_t)XPAR_AXI_GPIO_0_BASEADDR;
#endif
#if defined(XPAR_AXI_GPIO_0_GPIO_WIDTH)
    profile.gpio_width = (uint32_t)XPAR_AXI_GPIO_0_GPIO_WIDTH;
#endif
#if defined(XPAR_AXI_DMA_0_BASEADDR)
    profile.dma_base = (uint32_t)XPAR_AXI_DMA_0_BASEADDR;
#endif
#if defined(XPAR_AXI_DMA_0_INCLUDE_S2MM)
    profile.dma_has_s2mm = (uint32_t)XPAR_AXI_DMA_0_INCLUDE_S2MM;
#endif
#if defined(XPAR_AXI_IIC_0_BASEADDR)
    profile.iic_base = (uint32_t)XPAR_AXI_IIC_0_BASEADDR;
#endif
    return profile;
}
