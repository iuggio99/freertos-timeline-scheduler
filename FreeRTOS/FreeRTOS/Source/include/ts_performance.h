#ifndef __TS_PERFORMANCE__
#define __TS_PERFORMANCE__

#include "FreeRTOS.h"

/**
 * Returns the current value of the SysTick timer
 */
uint32_t ulGetSystickValue(void);

/**
 * Returns the number of CPU cycles between two SysTick readings
 */
uint32_t ulGetDurationCycles(uint32_t ulStart, uint32_t ulEnd);

#endif
