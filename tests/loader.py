from dataclasses import dataclass
from typing import List
import json
import glob


@dataclass
class Task:
    name: str
    type: str
    readyMs: int
    deadlineMS: int
    ulTaskRealDurationMs: int


@dataclass
class TestData:
    ulSubFrameMsLength: int
    ulMajorFrameSubFramesCount: int
    xTasks: List[Task]
    expectedOutput: List[str]


@dataclass
class Test:
    title: str
    description: str
    data: TestData


def load_json(path):
    with open(path, "r") as f:
        return json.load(f)


def validate_test_structure(json_data):
    # Check of the entire object
    if not isinstance(json_data, dict):
        raise ValueError("The JSON first element must be an object.")

    for field in ("title", "description", "data"):
        if field not in json_data:
            raise ValueError(f"Missing required field {field} in root.")

    if not isinstance(json_data["title"], str):
        raise ValueError("Title must be a string.")
    if not isinstance(json_data["description"], str):
        raise ValueError("Description must be a string.")

    data = json_data["data"]
    if not isinstance(data, dict):
        raise ValueError("Data must be an object.")

    # Check of data
    for field in ("ulSubFrameMsLength", "ulMajorFrameSubFramesCount", "xTasks", "expectedOutput"):
        if field not in data:
            raise ValueError(f"Missing required field {field} inside data.")

    if not isinstance(data["ulSubFrameMsLength"], int):
        raise ValueError("ulSubFrameMsLength must be an integer.")
    if not isinstance(data["ulMajorFrameSubFramesCount"], int):
        raise ValueError("ulMajorFrameSubFramesCount must be an integer.")

    # Check of tasks
    xTasks = data["xTasks"]
    if not isinstance(xTasks, list):
        raise ValueError("xTasks must be a list.")

    for i, t in enumerate(xTasks):
        if not isinstance(t, dict):
            raise ValueError(f"Task {i} must be an object.")

        for field in ("name", "type", "readyMs", "deadlineMS", "ulTaskRealDurationMs"):
            if field not in t:
                raise ValueError(f"Task {i} missing field '{field}'.")

        if not isinstance(t["name"], str):
            raise ValueError(f"Task {i} name must be a string.")

        if t["type"] not in ("SOFT", "HARD"):
            raise ValueError(f"Task {i} type must be SOFT or HARD.")

        for field in ("readyMs", "deadlineMS", "ulTaskRealDurationMs"):
            if not isinstance(t[field], int):
                raise ValueError(f"Task {i} {field} must be an integer.")

    # Check of the expected output
    expected = data["expectedOutput"]
    if not isinstance(expected, list):
        raise ValueError("ExpectedOutput must be a list.")
    for i, line in enumerate(expected):
        if not isinstance(line, str):
            raise ValueError(f"expectedOutput[{i}] must be a string.")


def class_mapping(json_data: dict) -> Test:
    # mapping of every single task
    tasks = []
    for t in json_data["data"]["xTasks"]:
        task_obj = Task(
            name=t["name"],
            type=t["type"],
            readyMs=t["readyMs"],
            deadlineMS=t["deadlineMS"],
            ulTaskRealDurationMs=t["ulTaskRealDurationMs"]
        )
        tasks.append(task_obj)

    # mapping of data section
    data_json = json_data["data"]
    data = TestData(
        ulSubFrameMsLength=data_json["ulSubFrameMsLength"],
        ulMajorFrameSubFramesCount=data_json["ulMajorFrameSubFramesCount"],
        xTasks=tasks,
        expectedOutput=data_json["expectedOutput"]
    )

    # mapping of the entire test
    test_obj = Test(
        title=json_data["title"],
        description=json_data["description"],
        data=data
    )
    return test_obj

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


def load_test(path):
    """
    - reads all test files
    """
    json_data = load_json(path)
    """
    - validates the json
    """
    validate_test_structure(json_data)
    """
    - casts the data to a class object of type TestData
    """
    return class_mapping(json_data)


def load_all_tests(folder_path):
    tests = []
    paths = []
    for path in glob.glob(f"{folder_path}/*.json"):
        try:
            paths.append(path)
        except Exception as e:
            print(f"Error loading path {path}: {e}")
    # sort paths to have a deterministic order of execution
    paths.sort()
    for path in paths:
        try:
            test = load_test(path)
            tests.append(test)
        except Exception as e:
            print(f"Error loading test from {path}: {e}")
    return tests
