from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
FORMAT_CMD = REPO_ROOT / "format.cmd"
BAD_FIXTURE = Path("src") / "format_options.cpp"
GOLDEN_FIXTURE = Path("src") / "format_options.golden.cpp"


def run_format(*args: str) -> subprocess.CompletedProcess[str]:
    command = subprocess.list2cmdline([str(FORMAT_CMD), "--root", str(TEST_ROOT), *args])
    return subprocess.run(
        ["cmd.exe", "/d", "/c", command],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def read_fixture(path: Path) -> str:
    return (TEST_ROOT / path).read_text(encoding="utf-8")


class FormatCommandTests(unittest.TestCase):
    maxDiff = None

    def test_bad_fixture_formats_to_golden_output(self) -> None:
        result = run_format("--file", str(BAD_FIXTURE), "--stdout")

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(GOLDEN_FIXTURE), result.stdout)

    def test_golden_fixture_is_idempotent(self) -> None:
        stdout_result = run_format("--file", str(GOLDEN_FIXTURE), "--stdout")

        self.assertEqual(
            0,
            stdout_result.returncode,
            msg=f"stdout:\n{stdout_result.stdout}\n\nstderr:\n{stdout_result.stderr}",
        )
        self.assertEqual(read_fixture(GOLDEN_FIXTURE), stdout_result.stdout)

        check_result = run_format("--file", str(GOLDEN_FIXTURE))

        self.assertEqual(
            0,
            check_result.returncode,
            msg=f"stdout:\n{check_result.stdout}\n\nstderr:\n{check_result.stderr}",
        )
        self.assertRegex(
            check_result.stdout,
            r"Checked 1 all file with native tree-sitter formatter in (?:\d+ms|\d+\.\d{3}s)\.\s*$",
        )

    def test_bad_fixture_is_rejected_by_check_mode(self) -> None:
        result = run_format("--file", str(BAD_FIXTURE))

        self.assertEqual(1, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertIn("Native formatting is required", result.stdout)

    def test_invalid_wrapper_usage_is_rejected(self) -> None:
        invalid_cases = [
            ("--stdout",),
            ("changed", "--file", str(BAD_FIXTURE)),
        ]

        for args in invalid_cases:
            with self.subTest(args=args):
                result = run_format(*args)

                self.assertEqual(2, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
                self.assertIn("Usage:", result.stdout)


if __name__ == "__main__":
    unittest.main()
