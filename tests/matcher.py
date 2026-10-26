from enum import Enum
from dataclasses import dataclass

from loader import Test
from runner import TestRunResult


class TestMatchStatus(Enum):
    SUCCESS = "success"
    ERROR = "error"


@dataclass
class TestMatchResult:
    """Represents the result of a test matching attempt."""
    status: TestMatchStatus


def strip_trailing_semicolons_per_line(text: str) -> str:
    lines = text.splitlines()
    cleaned = [line.rstrip().rstrip(";") for line in lines]
    return "\n".join(cleaned)


def parse_received_output(output: str) -> str:
    """Parses the output from QEMU to extract the relevant part for matching."""
    # For now, we assume that the relevant output is everything after the first two lines
    # which are related to QEMU startup and not relevant for the test matching.
    lines = output.splitlines()
    if len(lines) <= 2:
        return ""
    # Now, extract the 2nd major frame, so the string between 2 "[ ANY_NUMBER ] Major Frame Reset;" lines
    marker = "Major Frame Reset;"
    parts = output.split(marker)

    # We need at least: text + first reset + frame + second reset
    if len(parts) < 3:
        return ""

    # parts[1] = content AFTER first reset
    # parts[2] = content AFTER second reset
    # We want: parts[1] + marker
    frame = parts[1] + marker
    major_frame = frame.strip()

    # Now remove the trailing ";" from the line
    # print(f"Extracted major frame content before stripping: '{major_frame}'")
    major_frame = strip_trailing_semicolons_per_line(major_frame)
    # print(f"Extracted major frame content: '{major_frame}'")
    return major_frame


def split_output_lines(output: str) -> list[(int, str)]:
    """Splits the output into lines and returns a list of (timestamp, line_content).
    e.g. (20, Major Frame Reset)"""
    lines = output.split("\n")
    result = []
    for line in lines:
        if line.startswith("[") and "]" in line:
            try:
                timestamp_str, content = line.split("ms ]", 1)
                timestamp = int(timestamp_str[2:])
                result.append((timestamp, content.strip()))
            except ValueError:
                # If the line does not conform to the expected format, skip it
                continue
    return result


def match_outputs(expected: list[(int, str)], actual: list[(int, str)], time_tolerance: int = 1) -> bool:
    """Matches the expected output against the actual output, allowing for some time tolerance.
    Time tolerance is needed to accommodate the WCET of the scheduler
    """
    if len(expected) != len(actual):
        print(
            f"Output length mismatch: expected {len(expected)} lines, got {len(actual)} lines")
        print(f"Actual: {actual}")
        return False

    for (expected_time, expected_line), (actual_time, actual_line) in zip(expected, actual):
        if expected_line != actual_line:
            print(
                f"Line mismatch: expected '{expected_line}', got '{actual_line}'")
            return False
        if abs(expected_time - actual_time) > time_tolerance:
            print(
                f"Time mismatch: expected {expected_time} ms, got {actual_time} ms")
            return False

    return True


def match_test(test: Test, received_output: TestRunResult) -> TestMatchResult:
    """Matches the received output against the expected output for the test."""
    # print(f"Matching test: {test} with output: {received_output}")

    # concatenate stdout and stderr
    expected_output = split_output_lines(
        strip_trailing_semicolons_per_line("\n".join(test.data.expectedOutput).strip()))
    actual_output = split_output_lines(
        parse_received_output(received_output.stdout).strip())
    # Remove the first two lines from the actual output, as they are not relevant for the test matching
    # qemu-system-arm -machine mps2-an385  -cpu cortex-m3 -kernel \
    # ./Output/demo.elf -monitor none -nographic -serial stdio

    # Print the actual and expected outputs for debugging
    # print(f"Expected Output:\n{expected_output}")
    # print(f"Actual Output:\n{actual_output}")

    outputs_match = match_outputs(expected_output, actual_output)

    # check for match
    if not outputs_match:
        return TestMatchResult(status=TestMatchStatus.ERROR)

    return TestMatchResult(status=TestMatchStatus.SUCCESS)
