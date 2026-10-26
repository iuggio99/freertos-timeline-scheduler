#include "ts_uart.h"
#include "ts_scheduler_monitoring.h"

static QueueHandle_t monitorQueue;

static void vPrintTaskStart(TimelineTask_t *tsk, TickType_t time)
{
  UART_printf("[ %d ms ] %s start; \n", (unsigned)pdTICKS_TO_MS(time), tsk->pcTaskName);
}
static void vPrintTaskEnd(TimelineTask_t *tsk, TickType_t time)
{
  UART_printf("[ %d ms ] %s end; \n", (unsigned)pdTICKS_TO_MS(time), tsk->pcTaskName);
}
static void vPrintTaskDeadlineMissed(TimelineTask_t *tsk, TickType_t time)
{
  UART_printf("[ %d ms ] %s deadline missed; \n", (unsigned)pdTICKS_TO_MS(time), tsk->pcTaskName);
}
static void vPrintTaskPreemption(TimelineTask_t *tsk, TickType_t time)
{
  UART_printf("[ %d ms ] %s preempted; \n", (unsigned)pdTICKS_TO_MS(time), tsk->pcTaskName);
}
static void vPrintSchedulerTick(TickType_t time)
{
  UART_printf("[ %d ms ] Scheduler activated; \n", (unsigned)pdTICKS_TO_MS(time));
}
static void vPrintMajorFrameReset(TickType_t time)
{
  UART_printf("[ %d ms ] Major Frame Reset; \n", (unsigned)pdTICKS_TO_MS(time));
}
static void vPrintTaskKill(TimelineTask_t *tsk, TickType_t time)
{
  UART_printf("[ %d ms ] %s Killed; \n", (unsigned)pdTICKS_TO_MS(time), tsk->pcTaskName);
}

void vMonitoringStart(void *pvParameters)
{
  (void)pvParameters;
  MonitorMessage_t mon;
  for (;;)
  {
    if (xQueueReceive(monitorQueue, &mon, portMAX_DELAY) == pdTRUE)
    {
      switch (mon.code)
      {
      case MONITOR_TASK_START:
        vPrintTaskStart(mon.task, mon.ticks);
        break;
      case MONITOR_TASK_END:
        vPrintTaskEnd(mon.task, mon.ticks);
        break;
      case MONITOR_TASK_DEADLINE_MISSED:
        vPrintTaskDeadlineMissed(mon.task, mon.ticks);
        break;
      case MONITOR_TASK_PREEMPTION:
        vPrintTaskPreemption(mon.task, mon.ticks);
        break;
      case MONITOR_SCHEDULER_TICK:
        vPrintSchedulerTick(mon.ticks);
        break;
      case MONITOR_MAJOR_FRAME_RESET:
        vPrintMajorFrameReset(mon.ticks);
        break;
      case MONITOR_TASK_KILLED:
        vPrintTaskKill(mon.task, mon.ticks);
        break;
      default:
        UART_printf("Unable to recognize the monitoring \n");
      }
    }
    else
      UART_printf("Unable to obtain objects from monitoring queue \n");
  }
}

int32_t vMonitorEvent(MonitorEvent_t event, TimelineTask_t *tsk, TickType_t time)
{
  MonitorMessage_t mon;
  mon.task = tsk;
  mon.ticks = time;
  mon.code = event;
  if (xQueueSend(monitorQueue, &mon, pdMS_TO_TICKS(10)) == pdTRUE)
    return 0; // it works
  else
    return 1; // it doesn't work
}

void vMonitorTaskStart(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick)
{
  // Make the logging time relative to the major frame start
  vMonitorEvent(MONITOR_TASK_START, tsk, xTaskGetTickCount() - xMajorFrameStartTick);
}
void vMonitorTaskEnd(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick)
{
  vMonitorEvent(MONITOR_TASK_END, tsk, xTaskGetTickCount() - xMajorFrameStartTick);
}
void vMonitorTaskDeadlineMissed(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick)
{
  vMonitorEvent(MONITOR_TASK_DEADLINE_MISSED, tsk, xTaskGetTickCount() - xMajorFrameStartTick);
}
void vMonitorTaskPreemption(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick)
{
  vMonitorEvent(MONITOR_TASK_PREEMPTION, tsk, xTaskGetTickCount() - xMajorFrameStartTick);
}
void vMonitorSchedulerTick(TickType_t xMajorFrameStartTick)
{
  vMonitorEvent(MONITOR_SCHEDULER_TICK, NULL, xTaskGetTickCount() - xMajorFrameStartTick);
}
void vMonitorMajorFrameReset(TickType_t xMajorFrameStartTick)
{
  vMonitorEvent(MONITOR_MAJOR_FRAME_RESET, NULL, xTaskGetTickCount() - xMajorFrameStartTick);
}
void vMonitorTaskKill(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick)
{
  vMonitorEvent(MONITOR_TASK_KILLED, tsk, xTaskGetTickCount() - xMajorFrameStartTick);
}

void vMonitoringInitialise()
{
  monitorQueue = xQueueCreate(monitorQUEUE_LENGHT, sizeof(MonitorMessage_t));
  xTaskCreate(vMonitoringStart, "Function receiving monitor messages", 1000, NULL, monitorTASK_PRIORITY, NULL);
}
