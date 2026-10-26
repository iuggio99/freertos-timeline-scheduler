from dataclasses import dataclass
import os
import signal
import time
import subprocess
from loader import Test


@dataclass
class TestRunResult:
    """Represents the result of a test matching attempt."""
    stdout: str = ""
    stderr: str = ""
    return_code: int | None = None


# execute qemu with make qemu start, after a while let's stop the program and capture the output
def run_test(test: Test, timeout: int = 5) -> TestRunResult:
    try:
        print(f"Running test: {test.title} with timeout: {timeout} seconds")
        proc = subprocess.Popen(
            # start makefile
            ["make", "qemu_start"],
            # intercept output and errors
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd="../Demo",           # directory containing the Makefile
            # set the output as tet not bytes
            text=True,
            # set a line buffer, so that the lines arrive once at time
            bufsize=1,
            start_new_session=True  # Start a new process group
        )

        try:
            # let's collect everything after that qemu ends
            stdout, stderr = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            # if the timeout expires before qemu end, let's kill qemu and collect all the data
            os.killpg(proc.pid, signal.SIGKILL)
            stdout, stderr = proc.communicate()
        return TestRunResult(stdout=stdout, stderr=stderr, return_code=proc.returncode)

    except Exception as e:
        print(f"Error while running test '{test.title}': {e}")
        return TestRunResult(
            stdout="",
            stderr=str(e),
            return_code=None
        )
