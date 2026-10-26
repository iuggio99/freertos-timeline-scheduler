#include "ts_performance.h"

#define portNVIC_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xe000e018))
#define portNVIC_SYSTICK_LOAD_VALUE_REG (*((volatile uint32_t *)0xe000e014))

uint32_t ulGetSystickValue(void)
{
    return portNVIC_SYSTICK_CURRENT_VALUE_REG;
}

uint32_t ulGetDurationCycles(uint32_t ulStart, uint32_t ulEnd)
{
    uint32_t load_val = portNVIC_SYSTICK_LOAD_VALUE_REG;

    uint32_t duration;
    if (ulEnd <= ulStart)
    {
        duration = ulStart - ulEnd;
    }
    else
    {
        // A tick interrupt happened in between
        duration = (ulStart + (load_val - ulEnd));
    }

    return duration;
}