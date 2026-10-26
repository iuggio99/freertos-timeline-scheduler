#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stdio.h>

#include "ts_uart.h"
#include "ts_scheduler.h"
#include "ts_tasks.h"

/* Hardware init (QEMU / board specific) */
void prvHardwareInit(void);

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	prvHardwareInit();

	vConfigureScheduler(&xTimelineConfig);
	vStartTimelineScheduler();

	vTaskStartScheduler();
	UART_printf("Hello World \n");

	/* Should never reach here */
	for (;;)
	{
	}
}

void prvHardwareInit(void)
{
	/* Platform specific init (clock, UART, etc.) */
	UART_init();
}
