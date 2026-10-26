
from dataclasses import dataclass
from compiler import compile_test, CompilationStatus
from loader import Task, Test, TestData, load_all_tests
from matcher import TestMatchStatus, match_test
from runner import run_test


@dataclass
class TestsStartupEnvironment:
    initial_tasks_content: str


def startup() -> TestsStartupEnvironment:
    """
    Reads the initial value of ../Demo/Tasks.c file to restore it later.
    """
    with open("../Demo/ts_tasks.c", "r") as f:
        initial_tasks_content = f.read()
    return TestsStartupEnvironment(initial_tasks_content=initial_tasks_content)


def cleanup(startup_environment: TestsStartupEnvironment):
    """
    Restores the initial value of ../Demo/Tasks.c file.
    """
    with open("../Demo/ts_tasks.c", "w") as f:
        f.write(startup_environment.initial_tasks_content)


def main():
    print("Reading tests...")

    startup_content = startup()

    tests = load_all_tests("./testsData")

    # iterate tests and run them
    print("Running tests...")
    print("------------------------------------------------------")
    i = 1
    passed = 0
    failed = 0
    total = len(tests)
    for test in tests:
        print(f"[{i}/{total}] start: {test.title}\n{test.description}")
        compile_result = compile_test(test)
        if compile_result.status == CompilationStatus.ERROR:
            print(f"    -> [COMPILATION FAILED] {test.title}")
            continue

        run_result = run_test(test)

        match_result = match_test(test, run_result)

        if match_result.status == TestMatchStatus.SUCCESS:
            print(f"    -> [TEST PASSED] {test.title}")
            passed += 1
        else:
            print(f"    -> [TEST FAILED] {test.title}")
            failed += 1
        i += 1

    print("\n\n------------------------------------------------------")
    print("All tests done.")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")

    cleanup(startup_content)


if __name__ == "__main__":
    main()
