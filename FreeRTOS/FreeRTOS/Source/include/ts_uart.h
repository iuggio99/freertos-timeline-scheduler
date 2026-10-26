#ifndef __PRINTF__
#define __PRINTF__

#include "FreeRTOS.h"

/**
 * Implementation is hardware-dependent, so the responsibility to implement these functions
 * is left to the application developer
 */
void UART_init(void);
void UART_printf(const char *s, ...);

#endif
