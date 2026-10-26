#ifndef TS_SCHEDULER_H
#define TS_SCHEDULER_H

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stdbool.h>

/**
 * Priority of the scheduler task
 */
#define TS_SCHEDULER_TASK_PRIORITY (tskIDLE_PRIORITY + 5)

/**
 * Priority of any task managed by the scheduler
 */
#define TS_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

#define MAX_HRT_TASKS 32
#define MAX_SRT_TASKS 32

typedef enum
{
  /**
   * Deadline can be occasionally missed without catastrophic failures
   */
  TASK_TYPE_SOFT = 0,
  /**
   * Deadline must be met, otherwise the system is considered failed
   */
  TASK_TYPE_HARD = 1
} TimelineTaskType_t;

typedef struct
{
  /**
   * Task name
   */
  const char *pcName;

  /**
   * Function of the task
   */
  TaskFunction_t pvTaskCode;

  /**
   * Arguments
   */
  void *pvParameters;

  /**
   * Soft or Hard
   */
  TimelineTaskType_t xType;

  /**
   * ReadyInstant, Time (in ms) when the task becomes eligible
   */
  uint32_t ulReadyMs;

  /**
   * Absolute Deadline
   */
  uint32_t ulDeadlineMs;
} TimelineTaskConfig_t;

typedef struct
{
  /**
   * How long is a subFrame in milliseconds
   */
  uint32_t ulSubFrameMsLength;
  /**
   * How many subFrames compose a major frame
   */
  uint32_t ulMajorFrameSubFramesCount;

  /**
   * Array of tasks to be scheduled
   */
  TimelineTaskConfig_t *xTasks;

  /**
   * Amount of tasks in the array
   */
  uint32_t ulTasksCount;
} TimelineConfig_t;

typedef struct
{
  const char *pcTaskName;
  /**
   * User-defined task function
   */
  TaskFunction_t pvTaskFunction;

  /**
   * User-defined parameters for the task function
   */
  void *pvParameters;

  /**
   * Size of the task stack in words
   */
  configSTACK_DEPTH_TYPE usStackDepth;
  /**
   * FreeRTOS task handle
   */
  TaskHandle_t xTaskHandle;
  uint32_t ulStartTick; // start tick referring to the entire major cycle
  uint32_t ulEndTick;   // end tick referring to the entire major cycle
  /**
   * Subframe where the task runs
   */
  uint32_t ulSubFrameIndex; // Range from 0 to max -1
  /**
   * Indicates if the task has finished its execution in the current major frame
   */
  bool isFinished;
  /**
   * Indicates if the srt task deadline missed has been already notified
   */
  bool srtDeadlineMissedReported;

} TimelineTask_t;

typedef struct
{
  /**
   * Number of tasks
   */
  uint32_t ulTasksCount;
  /**
   * Array of tasks, sorted by ulStart_tick ascending
   * This way the scheduler can easily find the next task to run
   *
   * Note: to avoid using dynamic memory allocation, we use a fixed size array
   */
  TimelineTask_t xTasks[MAX_HRT_TASKS];
} TimelineHrtTasks_t;

typedef struct
{
  /**
   * Number of tasks
   */
  uint32_t ulTasksCount;
  /**
   * Array of tasks, sorted by ulStart_tick ascending
   * This way the scheduler can easily find the next task to run
   *
   * Note: to avoid using dynamic memory allocation, we use a fixed size array
   */
  TimelineTask_t xTasks[MAX_SRT_TASKS];
} TimelineSrtTasks_t;

/**
 * Scheduler task function that will run whenever any timeline task ends
 * before the minor cycle its execution and notifies it
 */
void vTsSchedulerTask(void *arg);

/**
 * Run a single hard real-time task
 * A hard real time task cannot be preempted by a soft real time task
 */
void vRunTimelineHrtTask(TimelineTask_t *xTask);

/**
 * Run a single soft real-time task
 * Pay attention that a soft real time task may be preempted by a hard real time task:
 * - check what is the next hard real time task to be executed and set a timer to wake up the scheduler at that time
 * it even checks if the soft real-time hasn't finished before its declared deadline
 */
void vRunTimelineSrtTask(TimelineTask_t *xTask);

/**
 * Resets the timeline scheduler to its initial state at the end of the timeline major cycle
 */
void vResetScheduler(void);

/**
 * Wraps a user-defined task function to notify the timeline scheduler upon its completion
 */
void xWrapTaskFunction(void *pvParameters);

/**
 * Configures the data structures and parameters for the timeline scheduler with the provided array of tasks
 *
 * @param xCfg Pointer to an array of TimelineTaskConfig_t structures defining the tasks and their scheduling parameters
 */
void vConfigureScheduler(TimelineConfig_t *xCfg);

/**
 * Starts the timeline scheduler by creating and starting a periodic timer
 * that triggers the scheduler tick at each subframe interval
 */
void vStartTimelineScheduler(void);

/**
 * It is called by the vRunTimeLineSrtTask to launch the monitor function and give the control to the scheduler when a soft real-time task misses its deadline
 */
void vOnHrtTaskDeadlineMiss(TimerHandle_t xTimer);
#endif // TS_SCHEDULER_H