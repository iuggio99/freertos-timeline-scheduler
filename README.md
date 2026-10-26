- [Timeline scheduler - group21](#timeline-scheduler---group21)
  - [Prerequisites](#prerequisites)
  - [Getting started](#getting-started)
    - [Clone the repository](#clone-the-repository)
    - [Build the project](#build-the-project)
    - [Run the scheduler](#run-the-scheduler)
    - [Observe the output](#observe-the-output)
    - [Clean (optional)](#clean-optional)
  - [Configuring the scheduler](#configuring-the-scheduler)
    - [Task implementation](#task-implementation)
    - [Task configuration](#task-configuration)
    - [Timeline configuration](#timeline-configuration)
  - [Scheduler policies](#scheduler-policies)
    - [Minor cycle exceeded](#minor-cycle-exceeded)
    - [Hard real time miss](#hard-real-time-miss)
    - [Soft real time miss](#soft-real-time-miss)
  - [Performance](#performance)
    - [Overhead](#overhead)
    - [Release jitter](#release-jitter)
  - [Testing](#testing)
  - [Licence](#licence)


# FreeRTOS - custom timeline scheduler

This is a custom implementation of a timeline scheduler for FreeRTOS. The scheduler is designed to manage tasks with hard and soft real-time constraints, ensuring that tasks are executed according to their specified timing requirements.

## Prerequisites

- GNU make
- GNU arm embedded toolchain (`arm-none-eabi-`):
  - `arm-none-eabi-gcc`
  - `arm-none-eabi-ld`
  - `arm-none-eabi-size`

## Getting started

This section explains how to compile and run the scheduler demo using QEMU through terminal commands.

### Clone the repository

Open a terminal and clone the repository of the project:

```sh
git clone ...
```

### Build the project

Move to the demo folder:

```sh
cd ./Demo
```

Compile FreeRTOS and the scheduler:

```sh
make all
```
This command builds all object files and links them into the final ELF executable using the provided linker script.

### Run the scheduler

After a successful build, start the emulator:

```sh
make qemu_start
```

This launches QEMU with the generated ELF image and boots the system.

### Observe the output

The scheduler runs directly inside QEMU and prints its behavior to the terminal through the serial interface. You can monitor:

- task execution
- scheduling decisions
- timing behavior

No additional tools are required.

### Clean (optional)

To remove compiled files:

```sh
make clean
```

## Configuring the scheduler

Tasks executed by the scheduler are defined in:

```sh
./Demo/ts_tasks.c
```
All task behavior and timing parameters are configured inside this file.
The file is divided into three logical sections:

1) Task implementation – the actual code executed by each task

2) Task configuration – scheduling parameters and metadata for each task

3) Timeline configuration – global timing structure of the scheduler

### Task implementation

In the first part of the file you define the task functions.
Each task is a standard FreeRTOS task:

```c
void vTask_A(void *pvParameters)
{
    (void)pvParameters;
    vBusyWait(4);
}
```

The function contains the workload that the scheduler must execute.

In this demo, tasks simulate computation using vBusyWait(ms), which blocks the task for a specific number of milliseconds.
You can implement any custom logic here (computation, I/O, control algorithms, etc.).

### Task configuration

In the second part of the file, tasks are registered inside the xTaskConfigs array:

```c
static TimelineTaskConfig_t xTaskConfigs[] =
{
    {
        .pcName       = "Task A",
        .pvTaskCode   = vTask_A,
        .pvParameters = NULL,
        .xType        = TASK_TYPE_HARD,
        .ulReadyMs    = 4,
        .ulDeadlineMs = 10
    }
};
```

Each element describes how and when a task must be scheduled.

Fields description:

1) pcName

Human-readable task name used for debugging and logging.

2) pvTaskCode

Pointer to the task function to execute.
This must match the function defined in the implementation section.

3) pvParameters

Pointer to optional parameters passed to the task at startup.
Use NULL if not needed.

4) xType

Task type handled by the scheduler.

TASK_TYPE_HARD → hard real-time

TASK_TYPE_SOFT → soft real-time

5) ulReadyMs

Time (in milliseconds) at which the task becomes ready to run inside the timeline.
Before this instant, the task will not be scheduled.

6) ulDeadlineMs

Deadline (in milliseconds) by which the task should be complete.

The scheduler uses this value to prioritize execution and enforce timing constraints.

### Timeline configuration

In the third part of the file, the global timeline is defined:

```c
TimelineConfig_t xTimelineConfig =
{
    .ulSubFrameMsLength         = 20,
    .ulMajorFrameSubFramesCount = 5,
    .xTasks                     = xTaskConfigs,
    .ulTasksCount               = sizeof(xTaskConfigs) / sizeof(xTaskConfigs[0])
};
```
Field description:

1) ulSubFrameMsLength

Duration (ms) of a single subframe.

2) ulMajorFrameSubFramesCount

Number of subframes inside one major frame.

3) xTasks

Pointer to the task configuration array.

4) ulTasksCount

Total number of configured tasks.


To add a task:

- Implement the task function

- Add a new entry inside xTaskConfigs

- Rebuild the project (make all)

No other changes are required.

## Scheduler policies

### Minor cycle exceeded

If a task is still running when the new minor cycle starts, we consider that a minor cycle has been exceeded. In this case, the scheduler kills the task and logs the event. The scheduler then continues with the next task in the schedule.

### Hard real time miss

If a hard real time task misses its deadline, the scheduler kills the task and logs the event.

### Soft real time miss

If a soft real time task misses its deadline, the scheduler logs the event but does not kill the task. The task is allowed to continue running, but it is marked as having missed its deadline.


## Performance

To accurately measure the performance of the scheduler, we need to use high-precision timers. For this reason, software timers are not suitable for this task (e.g. `xTaskGetTickCount()` has precision limited to 1 tick), so we need to use hardware timers.

So in `ts_performance` we use:

```c
#define portNVIC_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xe000e018))
#define portNVIC_SYSTICK_LOAD_VALUE_REG (*((volatile uint32_t *)0xe000e014))
```

- `portNVIC_SYSTICK_LOAD_VALUE_REG` -> defines the reload value, so the starting point of the countdown
- `portNVIC_SYSTICK_CURRENT_VALUE_REG` -> a high-speed hardware counter that decrements exactly once per CPU clock cycle


For the performance tests we used:

- in `FreeRTOSConfig.h`:
  - `#define configCPU_CLOCK_HZ ((unsigned long)25000000)`: set the CPU clock frequency to 25 MHz, which means each cycle takes 40 ns
  - `#define configTICK_RATE_HZ ((TickType_t)1000)`: set the tick rate to 1000 Hz, which means a tick duration of 1 ms
- test suite 6 (see [tests](#testing)) which has 8 tasks
- the data points were taken by running the program for 10 seconds

### Overhead

The overhead of the scheduler can be defined as the time needed to run the `vRunSchedulerTick` function, which is called every time the scheduler is activated.

To measure it, modify `ts_scheduler.c` as follows:

```c
static uint32_t ulStartVal = 0;

// ...

void vRunSchedulerTick()
{
    // ...

    // when activating vRunSchedulerTick take the initial measure
    ulStartVal = ulGetSystickValue();
}

// ...

void vCleanupSchedulerTick()
{
    // ...

    // when doing the cleanup measure the end time
    uint32_t ulEndVal = ulGetSystickValue();
    uint32_t ulDuration = ulGetDurationCycles(ulStartVal, ulEndVal);

    UART_printf("%d\n", ulDuration);
}
```

Then run the program and process the data points:

```sh
cd ./Demo
make all
make qemu_start > data.csv
```


Results:

|     | Cycles | Time(us) | Overhead (%) |
| --- | ------ | -------- | ------------ |
| Min | 12     | 0.48     | 0.00005      |
| Avg | 386.1  | 15.44    | 0.00154      |
| Max | 7213   | 288.52   | 0.02885      |

### Release jitter

The release jitter can be defined as the time between the moment a task is scheduled to run and the moment it actually starts running.

To measure it, modify `ts_scheduler.c` as follows:

```c
static uint32_t ulStartVal = 0;

// ...

void xWrapTaskFunction(void *pvParameters)
{
    TimelineTask_t *task = (TimelineTask_t *)pvParameters;

    uint32_t ulEndVal = ulGetSystickValue();
    uint32_t ulDuration = ulGetDurationCycles(ulStartVal, ulEndVal);

    UART_printf("%d\n", ulDuration);
    // ...
}

// ...

void vRunTimelineHrtTask(TimelineTask_t *xTask) {
    // ...
    ulStartVal = ulGetSystickValue();
    vTaskResume(xTask->xTaskHandle);
}

// ...

void vRunTimelineSrtTask(TimelineTask_t *xTask) {
    // ...
    ulStartVal = ulGetSystickValue();
    vTaskResume(xTask->xTaskHandle);
}
```

Then run the program and process the data points:

```sh
cd ./Demo
make all
make qemu_start > data.csv
```

Results:

|     | Cycles | Time(us) | Ticks   |
| --- | ------ | -------- | ------- |
| Min | 12     | 0.48     | 0.00048 |
| Avg | 369.2  | 14.76    | 0.01477 |
| Max | 5696   | 227.84   | 0.22784 |


## Testing

Prerequisites:

- `python 3.12`


To test the program automatically we use a python script that:

1. Computes a `ts_tasks.c` file from a json file that defines the tasks to be created
2. Compiles the program and runs it in QEMU
3. Compares the output of the program with the expected output

To run the test suite:

```sh
cd ./tests
python3 main.py
```

A test is defined with a .json file in the folder `./tests/testsData` following this format:

```json
{
    // title and description are used for logging purposes
    "title": string,
    "description": string,
    "data": {
        // the length of a subframe in ms
        "ulSubFrameMsLength": number,
        // the number of subframes in a major frame
        "ulMajorFrameSubFramesCount": number,
        // the tasks to be created
        "xTasks": Array<{
            // the name of the task, used for logging purposes
            "name": string,
            // the type of the task, either HARD or SOFT
            "type": "HARD" | "SOFT",
            // the time at which the task should be released in ms
            "readyMs": number,
            // the time at which the task should be completed in ms
            "deadlineMS": number,
            // the time needed to complete the task in ms
            "ulTaskRealDurationMs": number
        }>,
        // the expected output of the program, an array of strings that should be printed by the program in order
        "expectedOutput": string[]
    }
}
```

## Licence

This project is licensed under the MIT License.