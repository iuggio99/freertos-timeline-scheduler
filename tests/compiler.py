import subprocess
from dataclasses import dataclass
from enum import Enum

from loader import Task, Test, TestData


class CompilationStatus(Enum):
    SUCCESS = "success"
    ERROR = "error"


@dataclass
class CompilationResult:
    """Represents the result of a compilation attempt."""
    status: CompilationStatus
    stdout: str = ""
    stderr: str = ""
    return_code: int | None = None


def run_make_all():
    """
    Runs 'make all' in the ../Demo directory and captures the output.
    """
    result = subprocess.run(
        ["make", "all"],
        cwd="../Demo",           # directory containing the Makefile
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    stdout = result.stdout
    stderr = result.stderr
    return_code = result.returncode

    # print(stdout)
    # print(stderr)

    return stdout, stderr, return_code


def compute_tasks_file_content(test: Test) -> str:
    tasks = test.data.xTasks

    def sanitize_name(name: str) -> str:
        return name.replace(" ", "_")

    # Generate task function implementations
    busy_wait_function = """
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
}"""
    task_functions = []
    task_configs = []

    for task in tasks:
        cname = sanitize_name(task.name)
        duration = task.ulTaskRealDurationMs
        task_type = f"TASK_TYPE_{task.type}"

        # Use a simple vTaskDelay to simulate task execution
        task_functions.append(
            f"""
void v{cname}(void *pvParameters)
{{
    (void)pvParameters;
    vBusyWait({duration});
}}
""".strip()
        )

        task_configs.append(
            f"""
    {{
        .pcName       = "{task.name}",
        .pvTaskCode   = v{cname},
        .pvParameters = NULL,
        .xType        = {task_type},
        .ulReadyMs    = {task.readyMs},
        .ulDeadlineMs = {task.deadlineMS}
    }}
""".strip()
        )

    tasks_array = ",\n".join(task_configs)

    return f"""
#include "ts_tasks.h"
#include "FreeRTOS.h"
#include "task.h"

// busy wait function

{busy_wait_function}

// tasks functions

{chr(10).join(task_functions)}

// tasks configurations

static TimelineTaskConfig_t xTaskConfigs[] =
{{
{tasks_array}
}};

TimelineConfig_t xTimelineConfig =
{{
    .ulSubFrameMsLength         = {test.data.ulSubFrameMsLength},
    .ulMajorFrameSubFramesCount = {test.data.ulMajorFrameSubFramesCount},
    .xTasks                     = xTaskConfigs,
    .ulTasksCount               = sizeof(xTaskConfigs) / sizeof(xTaskConfigs[0])
}};
""".strip()


def write_tasks_file(test: Test):
    """
    Writes the tasks configuration file for the given test.
    """

    tasks_content = compute_tasks_file_content(test)

    with open("../Demo/ts_tasks.c", "w") as f:
        f.write(tasks_content)


def compile_test(test: Test) -> CompilationResult:
    """
    Writes the tasks file
    Compiles the project using 'make all'
    Returns the compilation result
    """
    write_tasks_file(test)

    out, err, code = run_make_all()

    # Check if there are compilation errors
    if code != 0:
        print(f"Compilation failed with return code {code}")
        print("=== STDOUT ===")
        print(out)
        print("=== STDERR ===")
        print(err)
        return CompilationResult(
            status=CompilationStatus.ERROR,
            stdout=out,
            stderr=err,
            return_code=code
        )

    return CompilationResult(
        status=CompilationStatus.SUCCESS,
        stdout=out,
        stderr=err,
        return_code=code
    )


# input_data = {
#     "title": "Test 1",
#     "description": "First essential test",
#     "data": {
#         "ulSubFrameMsLength": 5,
#         "ulMajorFrameSubFramesCount": 5,
#         "xTasks": [
#             {
#                 "name": "Task A",
#                 "type": "SOFT",
#                 "readyMs": 10,
#                 "deadlineMS": 15,
#                 "ulTaskRealDurationMs": 5

#             },
#             {
#                 "name": "Task B",
#                 "type": "HARD",
#                 "readyMs": 12,
#                 "deadlineMS": 17,
#                 "ulTaskRealDurationMs": 3

#             }
#         ],
#         "expectedOutput": [
#             "[10 ms] Task A starts",
#             "[12 ms] Task A stopped",
#             "[13 ms] Task B starts"
#         ]
#     }
# }


# test = Test(
#     title=input_data["title"],
#     description=input_data["description"],
#     data=TestData(
#         ulSubFrameMsLength=input_data["data"]["ulSubFrameMsLength"],
#         ulMajorFrameSubFramesCount=input_data["data"]["ulMajorFrameSubFramesCount"],
#         xTasks=[
#             Task(
#                 name=task["name"],
#                 type=task["type"],
#                 readyMs=task["readyMs"],
#                 deadlineMS=task["deadlineMS"],
#                 ulTaskRealDurationMs=task["ulTaskRealDurationMs"],
#             )
#             for task in input_data["data"]["xTasks"]
#         ],
#         expectedOutput=input_data["data"]["expectedOutput"],
#     ),
# )

# compile_test(test)
