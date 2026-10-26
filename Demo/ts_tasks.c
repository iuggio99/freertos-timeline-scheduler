#include "ts_tasks.h"
#include "FreeRTOS.h"
#include "task.h"

// busy wait function


/**
 * Performing vTaskDelay(pdMS_TO_TICKS(MILLISECONDS)); does not work,
 * since vTaskDelay measures the delay from the time the task is blocked, which in our case
 * is not true, since a task can be suspended and resumed many times.
 * @example
 * Assume a task A with:
 * vTaskDelay(pdMS_TO_TICKS(9));
 * Between ms 0-5 A runs, then it is suspended between 5-10
 * At 10 it is resumed, but the delay has already been consumed, so the task will not run for 4ms after the resume,
 * but it will end immediately.
 *
 * The solution for our case, since our scheduler has ms-granularity is to wait one millisecond at a time
 */
void vBusyWait(uint32_t ulTimeMs)
{
    for (uint32_t i = 0; i < ulTimeMs; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// tasks functions

void vTask_A(void *pvParameters)
{
    (void)pvParameters;
    vBusyWait(4);
}
void vTask_B(void *pvParameters)
{
    (void)pvParameters;
    vBusyWait(4);
}
void vTask_C(void *pvParameters)
{
    (void)pvParameters;
    vBusyWait(7);
}

// tasks configurations

static TimelineTaskConfig_t xTaskConfigs[] =
{
{
        .pcName       = "Task A",
        .pvTaskCode   = vTask_A,
        .pvParameters = NULL,
        .xType        = TASK_TYPE_HARD,
        .ulReadyMs    = 4,
        .ulDeadlineMs = 10
    },
{
        .pcName       = "Task B",
        .pvTaskCode   = vTask_B,
        .pvParameters = NULL,
        .xType        = TASK_TYPE_HARD,
        .ulReadyMs    = 14,
        .ulDeadlineMs = 20
    },
{
        .pcName       = "Task C",
        .pvTaskCode   = vTask_C,
        .pvParameters = NULL,
        .xType        = TASK_TYPE_HARD,
        .ulReadyMs    = 23,
        .ulDeadlineMs = 31
    }
};

TimelineConfig_t xTimelineConfig =
{
    .ulSubFrameMsLength         = 20,
    .ulMajorFrameSubFramesCount = 5,
    .xTasks                     = xTaskConfigs,
    .ulTasksCount               = sizeof(xTaskConfigs) / sizeof(xTaskConfigs[0])
};