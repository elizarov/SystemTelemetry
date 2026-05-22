from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
FORMAT_CMD = REPO_ROOT / "format.cmd"
INPUT_FIXTURE = Path("src") / "format_test_input.cpp"
OUTPUT_FIXTURE = Path("src") / "format_test_output.cpp"


def run_format_with_root(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    command = subprocess.list2cmdline([str(FORMAT_CMD), "--root", str(root), *args])
    return subprocess.run(
        ["cmd.exe", "/d", "/c", command],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def run_format(*args: str) -> subprocess.CompletedProcess[str]:
    return run_format_with_root(TEST_ROOT, *args)


def read_fixture(path: Path) -> str:
    return (TEST_ROOT / path).read_text(encoding="utf-8")


class FormatCommandTests(unittest.TestCase):
    maxDiff = None

    def test_input_fixture_formats_to_expected_output(self) -> None:
        result = run_format("--file", str(INPUT_FIXTURE), "--stdout")

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(OUTPUT_FIXTURE), result.stdout)

    def test_output_fixture_is_idempotent(self) -> None:
        stdout_result = run_format("--file", str(OUTPUT_FIXTURE), "--stdout")

        self.assertEqual(
            0,
            stdout_result.returncode,
            msg=f"stdout:\n{stdout_result.stdout}\n\nstderr:\n{stdout_result.stderr}",
        )
        self.assertEqual(read_fixture(OUTPUT_FIXTURE), stdout_result.stdout)

        check_result = run_format("--file", str(OUTPUT_FIXTURE))

        self.assertEqual(
            0,
            check_result.returncode,
            msg=f"stdout:\n{check_result.stdout}\n\nstderr:\n{check_result.stderr}",
        )
        self.assertRegex(
            check_result.stdout,
            r"Checked 1 all file with native tree-sitter formatter in (?:\d+ms|\d+\.\d{3}s)\.\s*$",
        )

    def test_input_fixture_is_rejected_by_check_mode(self) -> None:
        result = run_format("--file", str(INPUT_FIXTURE))

        self.assertEqual(1, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertIn("Native formatting is required", result.stdout)

    def test_path_scope_checks_and_fixes_limited_directory_or_file(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_path_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            source_dir = root / "src" / "nested"
            source_dir.mkdir(parents=True)
            source = source_dir / "sample.cpp"
            source.write_text("int main(){return 1;}\n", encoding="utf-8")

            check_result = run_format_with_root(root, "--path", "src")

            self.assertEqual(1, check_result.returncode, msg=f"stdout:\n{check_result.stdout}\n\nstderr:\n{check_result.stderr}")
            self.assertIn("Native formatting is required for 1 file", check_result.stdout)

            fix_result = run_format_with_root(root, "fix", "--path", "src")

            self.assertEqual(0, fix_result.returncode, msg=f"stdout:\n{fix_result.stdout}\n\nstderr:\n{fix_result.stderr}")
            self.assertIn("Formatted 1 path file", fix_result.stdout)
            self.assertEqual("int main() {\n    return 1;\n}\n", source.read_text(encoding="utf-8"))

            file_check_result = run_format_with_root(root, "--path", "src/nested/sample.cpp")

            self.assertEqual(
                0,
                file_check_result.returncode,
                msg=f"stdout:\n{file_check_result.stdout}\n\nstderr:\n{file_check_result.stderr}",
            )
            self.assertIn("Checked 1 path file", file_check_result.stdout)

    def test_invalid_wrapper_usage_is_rejected(self) -> None:
        invalid_cases = [
            ("--stdout",),
            ("changed", "--file", str(INPUT_FIXTURE)),
            ("changed", "--path", "src"),
            ("--file", str(INPUT_FIXTURE), "--path", "src"),
            ("--path", "src", "--stdout"),
        ]

        for args in invalid_cases:
            with self.subTest(args=args):
                result = run_format(*args)

                self.assertEqual(2, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
                self.assertIn("Usage:", result.stdout)


if __name__ == "__main__":
    unittest.main()
