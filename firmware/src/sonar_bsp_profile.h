#ifndef SONAR_BSP_PROFILE_H
#define SONAR_BSP_PROFILE_H

#include "sonar_bsp_validation.h"

/* Read generated BSP declarations only. This does not access AXI peripherals. */
sonar_profile_t sonar_platform_profile(void);

sonar_audio_profile_t sonar_platform_audio_profile(void);
#endif
