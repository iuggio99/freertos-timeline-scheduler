#ifndef MONITOR_H
#define MONITOR_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "ts_scheduler.h"

#define monitorTASK_PRIORITY (tskIDLE_PRIORITY + 1) //!
#define monitorQUEUE_LENGHT 10

typedef enum
{
  MONITOR_TASK_START,
  MONITOR_TASK_END,
  MONITOR_TASK_DEADLINE_MISSED,
  MONITOR_TASK_PREEMPTION,
  MONITOR_SCHEDULER_TICK,
  MONITOR_MAJOR_FRAME_RESET,
  MONITOR_TASK_KILLED
} MonitorEvent_t;

typedef struct
{
  TimelineTask_t *task;
  MonitorEvent_t code;
  TickType_t ticks;
} MonitorMessage_t;

// void vPrintTaskStart(TimelineTask_t *tsk, TickType_t time);
// void vPrintTaskEnd(TimelineTask_t *tsk, TickType_t time);
// void vPrintTaskDeadlineMissed(TimelineTask_t *tsk, TickType_t time);
// void vPrintTaskRelease(TimelineTask_t *tsk, TickType_t time);
// void vPrintTaskOverrun(TimelineTask_t *tsk, TickType_t time);

void vMonitorTaskStart(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick);
void vMonitorTaskEnd(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick);
void vMonitorTaskDeadlineMissed(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick);
void vMonitorTaskPreemption(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick);
void vMonitorSchedulerTick(TickType_t xMajorFrameStartTick);
void vMonitorMajorFrameReset(TickType_t xMajorFrameStartTick);
void vMonitorTaskKill(TimelineTask_t *tsk, TickType_t xMajorFrameStartTick);

void vMonitoringStart(void *pvParameters);
// int32_t vMonitorEvent(MonitorEvent_t event, TimelineTask_t *tsk, TickType_t time);
void vMonitoringInitialise();

#endif
