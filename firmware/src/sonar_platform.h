#ifndef SONAR_PLATFORM_H
#define SONAR_PLATFORM_H

#include "sonar_profile.h"

/* Read generated BSP declarations only. This does not access AXI peripherals. */
sonar_profile_t sonar_platform_profile(void);

#endif
