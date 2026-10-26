#include "ts_scheduler.h"
#include "timers.h"
#include "ts_uart.h"
#include "task.h"
#include "ts_scheduler_monitoring.h"
#include "semphr.h"
#include "ts_performance.h"

/**
 * Timer that is activated at each minor cycle end
 */
static TimerHandle_t xSchedulerTimer = NULL;
static TaskHandle_t timelineSchedulerHandle = NULL;

// Timer to handle the release of a soft real-time task
static TimerHandle_t xHrtDeadlineTimer = NULL;
static TimerHandle_t xSrtDeadlineTimer = NULL;
static TimerHandle_t xDeferredSchedulerTickTimer = NULL;

/**ulDeadlineMs
 * Tracks the tick where the current major frame started
 */
static TickType_t xMajorFrameStartTick = 0;

/**
 * Task that is currently running
 */
static TimelineTask_t *xCurrentTask;

/**
 * Stores all the hard real-time tasks information
 */
static TimelineHrtTasks_t xHrtTasks;

/**
 * Stores all the soft real-time tasks information
 */
static TimelineSrtTasks_t xSrtTasks;
/**
 * Tracks the current minor frame within the major cycle
 */
static uint32_t ulCurrentSubframeIndex;

/**
 * How long is a subFrame in ticks
 */
static uint32_t ulSubFrameTicksLength;

/**
 * How many subFrames compose a major frame
 */
static uint32_t ulMajorFrameSubFramesCount;

/**
 * Initial configuration of the timeline scheduler, used to reset the scheduler at the end of the major cycle
 */
static TimelineConfig_t *xInitialConfig;

/**
 * Mutex to protect scheduler data structures
 */
static SemaphoreHandle_t xSchedulerMutex;

/**
 * Used for performance monitoring
 */
// static uint32_t ulStartVal = 0;

/**
 * Runs a single tick of the timeline scheduler. Called by the subframe timer
 */
void vRunSchedulerTickFromSubframeTimer(TimerHandle_t xTimer);

/**
 * Runs a single tick of the timeline scheduler. Called by timers
 * that are deferred from the subframe timer
 */
void vRunSchedulerTickFromDeferredTimer(TimerHandle_t xTimer);

/**
 * Does not need to check if we are at the end of the minor cycle, since this is checked by vRunSchedulerTickFromSubframeTimer
 */
void vRunSchedulerTick();

void vTsSchedulerTask(void *arg)
{
    (void)arg;

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        vRunSchedulerTick();
    }
}

/**
 * Gives control to the timeline scheduler task
 */
void vGiveTsSchedulerTask(void)
{
    if (timelineSchedulerHandle != NULL)
    {
        xTaskNotifyGive(timelineSchedulerHandle);
    }
}

void vStartTimelineScheduler(void)
{
    // Mark the start tick of the major frame
    xMajorFrameStartTick = xTaskGetTickCount();

    /* 2. Create the Software Timer */
    xSchedulerTimer = xTimerCreate(
        "SchedTmr",            // Name
        ulSubFrameTicksLength, // Period in Tick
        pdTRUE,                // AutoReload
        (void *)0,             // ID
        vRunSchedulerTickFromSubframeTimer);

    if (xSchedulerTimer != NULL)
    {
        /* 3. Avviamo il timer con 0 tick di attesa*/
        xTimerStart(xSchedulerTimer, 0);
        // UART_printf("Scheduler Timer Created & Started (Soft Timer mode)\n");
    }
    else
    {
        // UART_printf("Error: Could not create Scheduler Timer!\n");
    }

    /**
     * Create scheduler task
     */
    xTaskCreate(
        vTsSchedulerTask,
        "TSched",
        configMINIMAL_STACK_SIZE + 256,
        NULL,
        TS_SCHEDULER_TASK_PRIORITY,
        &timelineSchedulerHandle);

    vRunSchedulerTick();
}

/**
 * Deletes and resets the task timers
 */
void vResetTaskTimers()
{
    if (xHrtDeadlineTimer != NULL)
    {
        if (xTimerIsTimerActive(xHrtDeadlineTimer))
        {
            xTimerStop(xHrtDeadlineTimer, 0);
        }
    }
    if (xSrtDeadlineTimer != NULL)
    {
        if (xTimerIsTimerActive(xSrtDeadlineTimer))
        {
            xTimerStop(xSrtDeadlineTimer, 0);
        }
    }

    if (xDeferredSchedulerTickTimer != NULL)
    {
        if (xTimerIsTimerActive(xDeferredSchedulerTickTimer))
        {
            xTimerStop(xDeferredSchedulerTickTimer, 0);
        }
    }
}

void xWrapTaskFunction(void *pvParameters)
{
    TimelineTask_t *task = (TimelineTask_t *)pvParameters;

    // uint32_t ulEndVal = ulGetSystickValue();
    // uint32_t ulDuration = ulGetDurationCycles(ulStartVal, ulEndVal);

    // UART_printf("%d\n", ulDuration);

    task->pvTaskFunction(task->pvParameters);

    task->isFinished = pdTRUE;

    // Stop & delete deadline/next-HRT timers
    vResetTaskTimers();

    vMonitorTaskEnd(task, xMajorFrameStartTick);

    xCurrentTask = NULL;

    /* Notify timeline scheduler */
    vGiveTsSchedulerTask();
    // yield control to scheduler
    vTaskSuspend(NULL);
}

BaseType_t xTimelineTaskCreate(TimelineTask_t *task)
{
    return xTaskCreate(
        xWrapTaskFunction,
        task->pcTaskName,
        task->usStackDepth,
        task,
        TS_TASK_PRIORITY,
        &task->xTaskHandle);
}

/**
 * Called whenever a task misses a deadline
 */
void vOnHrtTaskDeadlineMiss(TimerHandle_t xTimer)
{

    (void)xTimer;

    if (xCurrentTask == NULL)
        return;

    if (xHrtDeadlineTimer != NULL)
    {
        // Stop the timer, do not delete it as it will be reused
        xTimerStop(xHrtDeadlineTimer, 0);
    }

    if (xCurrentTask->isFinished)
    {
        return;
    }

    /**
     * todo: do not call printf, just push the event in the queue
     */
    vMonitorTaskDeadlineMissed(xCurrentTask, xMajorFrameStartTick);
    // vPrintTaskDeadlineMissed(xCurrentTask, xTaskGetTickCount());
    // kill the current running task
    if (xCurrentTask != NULL && xCurrentTask->isFinished == pdFALSE)
    {
        vMonitorTaskKill(xCurrentTask, xMajorFrameStartTick);
        if (xCurrentTask->xTaskHandle != NULL)
        {
            vTaskDelete(xCurrentTask->xTaskHandle);
        }
        xCurrentTask->isFinished = pdTRUE;
        xCurrentTask->xTaskHandle = NULL; // Mark as deleted
    }
    vRunSchedulerTick();
}

void vOnSrtTaskDeadlineMiss(TimerHandle_t xTimer)
{

    (void)xTimer;

    if (xCurrentTask == NULL)
        return;

    if (xSrtDeadlineTimer != NULL)
    {
        // Stop the timer, do not delete it as it will be reused
        xTimerStop(xSrtDeadlineTimer, 0);
    }

    if (xCurrentTask->isFinished)
    {
        return;
    }

    /**
     * todo: do not call printf, just push the event in the queue
     */
    vMonitorTaskDeadlineMissed(xCurrentTask, xMajorFrameStartTick);
    xCurrentTask->srtDeadlineMissedReported = true;
}

void vRunTimelineHrtTask(TimelineTask_t *xTask)
{

    // Calculates the remaining time to reach the deadline of the task
    uint32_t startTaskTick = xTaskGetTickCount() - xMajorFrameStartTick;
    uint32_t remainingTime = xTask->ulEndTick - startTaskTick;
    // Calculates the end tick of the current sub frame
    uint32_t currentSubFrameEndTick = (xTask->ulSubFrameIndex + 1) * ulSubFrameTicksLength;

    // UART_printf("Running HRT Task from %d to %d ticks\n", (unsigned)xTask->ulStartTick, (unsigned)xTask->ulEndTick);

    if (xTask->ulEndTick < currentSubFrameEndTick && remainingTime > 0)
    {

        // set the deadline timer to monitor the task deadline miss
        xTimerChangePeriod(xHrtDeadlineTimer, remainingTime, 0);
        xTimerStart(xHrtDeadlineTimer, 0);
    }

    xCurrentTask = xTask;
    // Notify the monitoring module about the task start
    vMonitorTaskStart(xCurrentTask, xMajorFrameStartTick);

    // ulStartVal = ulGetSystickValue();
    vTaskResume(xTask->xTaskHandle);
}

void vRunTimelineSrtTask(TimelineTask_t *xTask)
{

    // if there is a previous timer still running, stop it
    vResetTaskTimers();

    // Calculates the remaining time to reach the deadline of the task
    uint32_t startTaskTick = xTaskGetTickCount() - xMajorFrameStartTick;
    uint32_t remainingTime = xTask->ulEndTick - startTaskTick;
    // Calculates the end tick of the current sub frame
    uint32_t currentSubFrameEndTick = (xTask->ulSubFrameIndex + 1) * ulSubFrameTicksLength;

    // if the deadline of the task arrives before the end of the sub frame, it will give the control
    // to the scheduler
    if (xTask->ulEndTick < currentSubFrameEndTick && remainingTime > 0)
    {
        xTimerChangePeriod(xSrtDeadlineTimer, remainingTime, 0);
        xTimerStart(xSrtDeadlineTimer, 0);
    }
    // let's start the control on the queue of the hard real-time tasks
    for (uint32_t i = 0; i < xHrtTasks.ulTasksCount; i++)
    {
        // if there is an hard real-time task with a start time which arrives before of the current soft task end time
        if (xHrtTasks.xTasks[i].ulStartTick < currentSubFrameEndTick && xHrtTasks.xTasks[i].isFinished == pdFALSE)
        {
            remainingTime = xHrtTasks.xTasks[i].ulStartTick - startTaskTick;

            if (remainingTime <= 0)
            {
                continue;
            }
            xTimerChangePeriod(
                xDeferredSchedulerTickTimer,
                remainingTime,
                0);
            xTimerStart(xDeferredSchedulerTickTimer, 0);
            break; // the array of the hard real-time task is already ordered, so once we found an hard
                   // real-time task to run it is useless to continue the for cycle
        }
    }

    // resume the soft-real time task
    xCurrentTask = xTask;
    vMonitorTaskStart(xCurrentTask, xMajorFrameStartTick);

    // ulStartVal = ulGetSystickValue();
    vTaskResume(xTask->xTaskHandle);
}

void vResetScheduler(void)
{
    /* We Stop and destroy the active timers of the old frame */
    vResetTaskTimers();

    /**
     * Mark the start tick of the major frame
     */
    xMajorFrameStartTick = xTaskGetTickCount();

    /* Now we iterate through the HRT Tasks, we destroy and recreate them */
    for (uint32_t i = 0; i < xHrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pTask = &xHrtTasks.xTasks[i];

        /* If Task exists(valid handle), we kill it */
        if (pTask->xTaskHandle != NULL)
        {

            vTaskDelete(pTask->xTaskHandle);
            pTask->xTaskHandle = NULL;
        }
        pTask->isFinished = pdFALSE;

        /* we recreate it calling xTimelineTaskCreate that will update pTask->xTaskHandle */
        if (xTimelineTaskCreate(pTask) != pdPASS)
        {
            // UART_printf("Error: Failed to recreate HRT Task %s during reset\n", pTask->pcTaskName);
        }
        else
        {
            vTaskSuspend(pTask->xTaskHandle);
        }
    }

    /* Now same thing with SOFT Real-Time Tasks*/
    for (uint32_t i = 0; i < xSrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pTask = &xSrtTasks.xTasks[i];

        if (pTask->xTaskHandle != NULL)
        {
            vTaskDelete(pTask->xTaskHandle);
            pTask->xTaskHandle = NULL;
        }
        pTask->isFinished = pdFALSE;
        pTask->srtDeadlineMissedReported = pdFALSE;
        if (xTimelineTaskCreate(pTask) != pdPASS)
        {
            // UART_printf("Error: Failed to recreate SRT Task %s during reset\n", pTask->pcTaskName);
        }
        else
        {
            vTaskSuspend(pTask->xTaskHandle);
        }
    }

    /* We reset scheduler's state variables */
    ulCurrentSubframeIndex = 0;
    xCurrentTask = NULL;
}

/**
 * Sort timeline tasks by start time (ascending).
 */
void prvSortTasksByStartTick(TimelineTask_t *pxTasks, uint32_t ulCount)
{
    //Insertion sort to order the array: for low quantities of elements it is optimal
    for (uint32_t i = 1; i < ulCount; i++)
    {
        TimelineTask_t xKey = pxTasks[i];
        int32_t j = (int32_t)i - 1;

        while (j >= 0 && pxTasks[j].ulStartTick > xKey.ulStartTick)
        {
            pxTasks[j + 1] = pxTasks[j];
            j--;
        }
        pxTasks[j + 1] = xKey;
    }

    // for (uint32_t i = 0; i < ulCount; i++)
    // {
    //     UART_printf("Task %s: start tick %d, end tick %d\n",
    //                 pxTasks[i].pcTaskName,
    //                 (unsigned)pxTasks[i].ulStartTick,
    //                 (unsigned)pxTasks[i].ulEndTick);
    // }
}

void vConfigureScheduler(TimelineConfig_t *xCfg)
{
    /*Basic null check*/
    if (xCfg == NULL)
    {
        return;
    }
    if (xCfg->ulSubFrameMsLength == 0 || xCfg->ulMajorFrameSubFramesCount == 0)
    {
        return;
    }
    if (xCfg->ulTasksCount == 0)
    {
        return;
    }

    xSchedulerMutex = xSemaphoreCreateMutex();

    ulSubFrameTicksLength = pdMS_TO_TICKS(xCfg->ulSubFrameMsLength);
    ulMajorFrameSubFramesCount = xCfg->ulMajorFrameSubFramesCount;

    /*Keep a pointer to the initial config*/
    xInitialConfig = xCfg;

    /*Reset runtime state*/
    ulCurrentSubframeIndex = 0;
    xCurrentTask = NULL;

    // initialize timers to avoid re-creating them every time
    xHrtDeadlineTimer = xTimerCreate(
        "DeadLineHrtTmr",
        ulSubFrameTicksLength,
        pdFALSE,
        (void *)0,
        vOnHrtTaskDeadlineMiss);
    xSrtDeadlineTimer = xTimerCreate(
        "DeadLineSrtTmr",
        ulSubFrameTicksLength,
        pdFALSE,
        (void *)0,
        vOnSrtTaskDeadlineMiss);
    xDeferredSchedulerTickTimer = xTimerCreate(
        "NextHrtTmr",
        ulSubFrameTicksLength,
        pdFALSE,
        (void *)1,
        vRunSchedulerTickFromDeferredTimer);

    /*Reset internal task container*/
    xHrtTasks.ulTasksCount = 0;
    xSrtTasks.ulTasksCount = 0;

    for (uint32_t i = 0; i < xCfg->ulTasksCount; i++)
    {
        TimelineTaskConfig_t *pxTaskCfg = &xCfg->xTasks[i];

        uint32_t startTick = pdMS_TO_TICKS(pxTaskCfg->ulReadyMs);
        uint32_t endTick = pdMS_TO_TICKS(pxTaskCfg->ulDeadlineMs);
        uint32_t subframeIndex = startTick / ulSubFrameTicksLength;

        TimelineTask_t *pxDstTask = NULL;

        /* Select destination list: HRT or SRT */
        if (pxTaskCfg->xType == TASK_TYPE_HARD)
        {
            if (xHrtTasks.ulTasksCount >= MAX_HRT_TASKS)
            {
                /* Too many HRT tasks: just ignore extra tasks */
                continue;
            }

            pxDstTask = &xHrtTasks.xTasks[xHrtTasks.ulTasksCount++];
        }
        else
        {
            if (xSrtTasks.ulTasksCount >= MAX_SRT_TASKS)
            {
                /* Too many SRT tasks */
                continue;
            }

            pxDstTask = &xSrtTasks.xTasks[xSrtTasks.ulTasksCount++];
        }

        /* Copy from "external config" to "internal task" */
        pxDstTask->pcTaskName = pxTaskCfg->pcName;
        pxDstTask->pvTaskFunction = pxTaskCfg->pvTaskCode;
        pxDstTask->pvParameters = pxTaskCfg->pvParameters;
        pxDstTask->isFinished = pdFALSE;
        pxDstTask->srtDeadlineMissedReported = pdFALSE;
        pxDstTask->usStackDepth = configMINIMAL_STACK_SIZE + 256;
        pxDstTask->ulStartTick = startTick;
        pxDstTask->ulEndTick = endTick;
        pxDstTask->ulSubFrameIndex = subframeIndex;
        pxDstTask->xTaskHandle = NULL;
    }

    /* 2. Sort tasks by start time before creating FreeRTOS tasks */
    prvSortTasksByStartTick(xHrtTasks.xTasks, xHrtTasks.ulTasksCount);
    prvSortTasksByStartTick(xSrtTasks.xTasks, xSrtTasks.ulTasksCount);

    /* 3. Create FreeRTOS tasks */
    /* Create HRT Tasks */
    for (uint32_t i = 0; i < xHrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pTask = &xHrtTasks.xTasks[i];
        xTimelineTaskCreate(pTask);
        if (pTask->xTaskHandle != NULL)
        {
            vTaskSuspend(pTask->xTaskHandle);
        }
    }

    /* Create SRT Tasks */
    for (uint32_t i = 0; i < xSrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pTask = &xSrtTasks.xTasks[i];
        xTimelineTaskCreate(pTask);
        if (pTask->xTaskHandle != NULL)
        {
            vTaskSuspend(pTask->xTaskHandle);
        }
    }

    vMonitoringInitialise();
}

void vRunSchedulerTickFromDeferredTimer(TimerHandle_t xTimer)
{
    (void)xTimer;

    // Run the actual scheduler tick
    vGiveTsSchedulerTask();
}

void vRunSchedulerTickFromSubframeTimer(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (xTimer == xSchedulerTimer)
    {
        // Check if at the end of the minor frame any task is running and if yes it kills it
        if (xCurrentTask != NULL && xCurrentTask->isFinished == false)
        {
            vMonitorTaskKill(xCurrentTask, xMajorFrameStartTick);
            if (xCurrentTask->xTaskHandle != NULL)
            {
                vTaskDelete(xCurrentTask->xTaskHandle);
            }
            xCurrentTask->xTaskHandle = NULL;
            xCurrentTask->isFinished = true;
            xCurrentTask = NULL;
        }

        /* We increment subframe current index */
        ulCurrentSubframeIndex++;

        /* MAJOR FRAME end control */
        if (ulCurrentSubframeIndex >= ulMajorFrameSubFramesCount)
        {
            vMonitorMajorFrameReset(xMajorFrameStartTick);
            /* End of the cycle, we reset */
            vResetScheduler();

            /*  ulCurrentSubframeIndex back at 0 after reset*/
            ulCurrentSubframeIndex = 0;
        }
    }

    // Run the actual scheduler tick
    vGiveTsSchedulerTask();
}

/**
 * Releases the scheduler mutex after a scheduler tick
 */
void vCleanupSchedulerTick()
{
    xSemaphoreGive(xSchedulerMutex);

    // uint32_t ulEndVal = ulGetSystickValue();
    // uint32_t ulDuration = ulGetDurationCycles(ulStartVal, ulEndVal);

    // UART_printf("%d\n", ulDuration);
}

void vRunSchedulerTick()
{

    /**
     * The scheduler can run only if the mutex is available
     * - if mutex is not available, it means that another scheduler tick is running
     * - in this case, we simply return and let the other tick finish its work
     *
     *
     * Remember to release the mutex at the end of the scheduler tick
     */
    if (xSemaphoreTake(xSchedulerMutex, (TickType_t)0) != pdTRUE)
    {
        return;
    }

    /**
     * Make the current time relative to the major frame start
     */
    TickType_t xNow = xTaskGetTickCount() - xMajorFrameStartTick;
    vMonitorSchedulerTick(xMajorFrameStartTick);
    // ulStartVal = ulGetSystickValue();

    vResetTaskTimers();

    // #region Search for tasks that start now or before now
    /* First try hard real-time tasks */
    for (uint32_t i = 0; i < xHrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pxCandidateTask = &xHrtTasks.xTasks[i];

        if (pxCandidateTask->isFinished || pxCandidateTask->xTaskHandle == NULL)
            continue;
        // ONLY run if the start time has arrived
        if (xNow < pxCandidateTask->ulStartTick)
            continue;

        // check if the candidate is not finished and its deadline is passed, log the deadline miss
        if (pxCandidateTask->isFinished == pdFALSE && xNow > pxCandidateTask->ulEndTick)
        {
            vMonitorTaskDeadlineMissed(pxCandidateTask, xMajorFrameStartTick);
            vTaskDelete(pxCandidateTask->xTaskHandle);
            pxCandidateTask->isFinished = true;
            pxCandidateTask->xTaskHandle = NULL; // Mark as deleted
            continue;
        }

        /* preempt current (if any) */
        if (xCurrentTask)
        {
            if (xCurrentTask->isFinished == pdFALSE)
            {
                vMonitorTaskPreemption(xCurrentTask, xMajorFrameStartTick);
            }
            if (xCurrentTask->xTaskHandle != NULL)
            {
                vTaskSuspend(xCurrentTask->xTaskHandle);
            }
        }

        vRunTimelineHrtTask(pxCandidateTask);
        vCleanupSchedulerTick();
        return;
    }

    /* Then soft real-time tasks */
    for (uint32_t i = 0; i < xSrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pxCandidateTask = &xSrtTasks.xTasks[i];

        if (pxCandidateTask->isFinished || pxCandidateTask->xTaskHandle == NULL)
            continue;

        // ONLY run if the start time has arrived
        if (xNow < pxCandidateTask->ulStartTick)
            continue;

        // check if the candidate is not finished and its deadline is passed, log the deadline miss
        if (pxCandidateTask->srtDeadlineMissedReported == pdFALSE && xNow > pxCandidateTask->ulEndTick && pxCandidateTask->isFinished == pdFALSE)
        {
            // For SRT tasks, simply log the deadline miss and continue
            vMonitorTaskDeadlineMissed(pxCandidateTask, xMajorFrameStartTick);
            pxCandidateTask->srtDeadlineMissedReported = true;
        }
        if (xCurrentTask)
        {
            if (xCurrentTask->isFinished == pdFALSE)
            {
                vMonitorTaskPreemption(xCurrentTask, xMajorFrameStartTick);
            }
            if (xCurrentTask->xTaskHandle != NULL)
            {
                vTaskSuspend(xCurrentTask->xTaskHandle);
            }
        }

        vRunTimelineSrtTask(pxCandidateTask);
        vCleanupSchedulerTick();
        return;
    }
    // #endregion

    // #region No task to run now, schedule next task if any
    /**
     * If we reach this point, no task has been scheduled.
     * It means either:
     * - all tasks are finished
     * - no task is ready to run yet -> maybe some task will become ready in this subframe in some ms
     *
     * Goal: find the next task to run in this subframe, if any, and set a timer to wake up the scheduler when its start time arrives
     */

    TimelineTask_t *pxCandidateTaskHrt = NULL;
    TimelineTask_t *pxCandidateTaskSrt = NULL;
    for (uint32_t i = 0; i < xHrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pxCandidateTask = &xHrtTasks.xTasks[i];
        if (pxCandidateTask->isFinished || pxCandidateTask->xTaskHandle == NULL)
            continue;

        /**
         * Only consider tasks that have not yet started
         */
        if (xNow > pxCandidateTask->ulStartTick)
            continue;

        // check if the candidate task starts in the current subframe
        uint32_t subframeStartTick = ulCurrentSubframeIndex * ulSubFrameTicksLength;
        uint32_t subframeEndTick = (ulCurrentSubframeIndex + 1) * ulSubFrameTicksLength;
        if (pxCandidateTask->ulStartTick >= subframeStartTick && pxCandidateTask->ulStartTick < subframeEndTick)
        {
            pxCandidateTaskHrt = pxCandidateTask;
            break;
        }
    }

    // check soft real-time tasks
    for (uint32_t i = 0; i < xSrtTasks.ulTasksCount; i++)
    {
        TimelineTask_t *pxCandidateTask = &xSrtTasks.xTasks[i];
        if (pxCandidateTask->isFinished || pxCandidateTask->xTaskHandle == NULL)
            continue;

        /**
         * Only consider tasks that have not yet started
         */
        if (xNow > pxCandidateTask->ulStartTick)
            continue;
        // check if the candidate task starts in the current subframe
        uint32_t subframeStartTick = ulCurrentSubframeIndex * ulSubFrameTicksLength;
        uint32_t subframeEndTick = (ulCurrentSubframeIndex + 1) * ulSubFrameTicksLength;
        if (pxCandidateTask->ulStartTick >= subframeStartTick && pxCandidateTask->ulStartTick < subframeEndTick)
        {
            pxCandidateTaskSrt = pxCandidateTask;
            break;
        }
    }
    if (pxCandidateTaskHrt == NULL && pxCandidateTaskSrt == NULL)
    {
        vCleanupSchedulerTick();
        return;
    }
    if (pxCandidateTaskHrt != NULL && pxCandidateTaskSrt == NULL)
    {
        // // found a task starting in this subframe
        uint32_t startDelay = pxCandidateTaskHrt->ulStartTick - xNow;

        if (startDelay <= 0)
        {
            vCleanupSchedulerTick();
            return;
        }

        xTimerChangePeriod(xDeferredSchedulerTickTimer, startDelay, 0);
        xTimerStart(xDeferredSchedulerTickTimer, 0);
        vCleanupSchedulerTick();
        return;
    }
    if (pxCandidateTaskHrt == NULL && pxCandidateTaskSrt != NULL)
    {
        uint32_t startDelay = pxCandidateTaskSrt->ulStartTick - xNow;
        if (startDelay <= 0)
        {
            vCleanupSchedulerTick();
            return;
        }

        xTimerChangePeriod(xDeferredSchedulerTickTimer, startDelay, 0);
        xTimerStart(xDeferredSchedulerTickTimer, 0);
        vCleanupSchedulerTick();
        return;
    }
    if (pxCandidateTaskHrt->ulStartTick <= pxCandidateTaskSrt->ulStartTick)
    {
        // // found a task starting in this subframe
        uint32_t startDelay = pxCandidateTaskHrt->ulStartTick - xNow;

        if (startDelay <= 0)
        {
            vCleanupSchedulerTick();
            return;
        }

        xTimerChangePeriod(xDeferredSchedulerTickTimer, startDelay, 0);
        xTimerStart(xDeferredSchedulerTickTimer, 0);
        vCleanupSchedulerTick();
        return;
    }
    else
    {
        uint32_t startDelay = pxCandidateTaskSrt->ulStartTick - xNow;
        if (startDelay <= 0)
        {
            vCleanupSchedulerTick();
            return;
            ;
        }

        xTimerChangePeriod(xDeferredSchedulerTickTimer, startDelay, 0);
        xTimerStart(xDeferredSchedulerTickTimer, 0);
        vCleanupSchedulerTick();
        return;
    }

    // #endregion
    // TickType_t xNow2 = xTaskGetTickCount() - xMajorFrameStartTick;
    // TickType_t xIdleTime = xNow2 - xNow;
    // UART_printf("Idle time in scheduler tick: %d ticks\n", (unsigned)xIdleTime);

    vCleanupSchedulerTick();
}
