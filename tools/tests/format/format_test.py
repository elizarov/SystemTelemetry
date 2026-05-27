from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
FORMAT_CMD = REPO_ROOT / "format.cmd"
FORMAT_EXE = REPO_ROOT / "build" / "CaseDashTools.exe"
INPUT_FIXTURE = Path("src") / "format_test_input.cpp"
OUTPUT_FIXTURE = Path("src") / "format_test_output_temp.cpp"


def native_format(*args: str, cwd: Path = REPO_ROOT, input_text: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(FORMAT_EXE), "format", *args],
        cwd=cwd,
        input=input_text,
        check=False,
        capture_output=True,
        text=True,
    )


def run_wrapper(*args: str) -> subprocess.CompletedProcess[str]:
    command = subprocess.list2cmdline([str(FORMAT_CMD), *args])
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

    def test_stdin_formats_to_expected_output(self) -> None:
        result = native_format("--style=file", cwd=TEST_ROOT, input_text=read_fixture(INPUT_FIXTURE))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(OUTPUT_FIXTURE), result.stdout)
        self.assertRegex(result.stderr, r"Formatted stdin in (?:\d+ms|\d+\.\d{3}s)\.\s*$")

    def test_golden_input_parses_without_recovery(self) -> None:
        result = native_format("--style=file", str(TEST_ROOT / INPUT_FIXTURE))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertNotIn("tree-sitter parse failed", result.stderr)

    def test_file_argument_formats_to_stdout(self) -> None:
        result = native_format("--style=file", str(TEST_ROOT / OUTPUT_FIXTURE))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(OUTPUT_FIXTURE), result.stdout)
        self.assertRegex(
            result.stderr,
            r"Formatted 1 file in (?:\d+ms|\d+\.\d{3}s)\.\s*$",
        )

    def test_dry_run_accepts_idempotent_file_and_rejects_unformatted_file(self) -> None:
        ok_result = native_format("--style=file", "--dry-run", str(TEST_ROOT / OUTPUT_FIXTURE))

        self.assertEqual(0, ok_result.returncode, msg=f"stdout:\n{ok_result.stdout}\n\nstderr:\n{ok_result.stderr}")
        self.assertRegex(
            ok_result.stdout,
            r"Checked 1 file in (?:\d+ms|\d+\.\d{3}s)\.\s*$",
        )

        bad_result = native_format("--style=file", "--dry-run", str(TEST_ROOT / INPUT_FIXTURE))

        self.assertEqual(1, bad_result.returncode, msg=f"stdout:\n{bad_result.stdout}\n\nstderr:\n{bad_result.stderr}")
        self.assertIn("Formatting is required for 1 file", bad_result.stdout)

    def test_files_option_reads_newline_file_list(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_files_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            file_list = root / "files.txt"
            file_list.write_text(f"{TEST_ROOT / OUTPUT_FIXTURE}\n\n", encoding="utf-8")

            result = native_format("--style=file", "--dry-run", "--files", str(file_list))

            self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertRegex(result.stdout, r"Checked 1 file in (?:\d+ms|\d+\.\d{3}s)\.\s*$")

    def test_in_place_formats_file(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_in_place_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            shutil.copyfile(REPO_ROOT / ".cpp-format", root / ".cpp-format")
            source = root / "sample.cpp"
            source.write_text("int main(){return 1;}\n", encoding="utf-8")

            result = native_format("--style=file", "-i", str(source), cwd=root)

            self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertEqual("int main() {\n    return 1;\n}\n", source.read_text(encoding="utf-8").replace("\r\n", "\n"))
            self.assertIn("Formatted 1 file", result.stdout)

    def test_declarator_reference_tokens_include_managed_cpp(self) -> None:
        result = native_format(
            "--style=file",
            input_text="void f(Object ^ handle,Object % tracking,int && moved,int * pointer){}\n",
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "void f(Object^ handle, Object% tracking, int&& moved, int* pointer) {\n}\n",
            result.stdout,
        )

    def test_parse_error_rejects_stdout_formatting(self) -> None:
        result = native_format("--style=file", input_text="int main( { return 1; }\n")

        self.assertEqual(1, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual("", result.stdout)
        self.assertIn("tree-sitter parse failed", result.stderr)

    def test_parse_error_does_not_write_in_place_batch(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_parse_error_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            shutil.copyfile(REPO_ROOT / ".cpp-format", root / ".cpp-format")
            valid = root / "valid.cpp"
            invalid = root / "invalid.cpp"
            valid.write_text("int main(){return 1;}\n", encoding="utf-8")
            invalid.write_text("int main( { return 1; }\n", encoding="utf-8")

            result = native_format("--style=file", "-i", str(valid), str(invalid), cwd=root)

            self.assertEqual(1, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertEqual("int main(){return 1;}\n", valid.read_text(encoding="utf-8").replace("\r\n", "\n"))
            self.assertIn("tree-sitter parse failed", result.stderr)

    def test_explicit_style_file_and_upward_discovery(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_style_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            nested = root / "a" / "b"
            nested.mkdir(parents=True)
            shutil.copyfile(REPO_ROOT / ".cpp-format", root / ".cpp-format")
            source = nested / "sample.cpp"
            source.write_text("int main(){return 1;}\n", encoding="utf-8")

            discovered = native_format("--dry-run", str(source), cwd=nested)
            explicit = native_format(f"--style={root / '.cpp-format'}", "--dry-run", str(source), cwd=nested)

            self.assertEqual(1, discovered.returncode, msg=f"stdout:\n{discovered.stdout}\n\nstderr:\n{discovered.stderr}")
            self.assertIn("Formatting is required", discovered.stdout)
            self.assertEqual(1, explicit.returncode, msg=f"stdout:\n{explicit.stdout}\n\nstderr:\n{explicit.stderr}")
            self.assertIn("Formatting is required", explicit.stdout)

    def test_ignore_file_skips_simple_directory_entries(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_ignore_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            vendor = root / "src" / "vendor"
            vendor.mkdir(parents=True)
            shutil.copyfile(REPO_ROOT / ".cpp-format", root / ".cpp-format")
            (root / ".cpp-format-ignore").write_text("src/vendor\n", encoding="utf-8")
            source = vendor / "ignored.cpp"
            source.write_text("int main(){return 1;}\n", encoding="utf-8")

            result = native_format("--dry-run", str(source), cwd=root)

            self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertIn("Checked 0 files", result.stdout)
            self.assertIn("Skipped 1 ignored file", result.stdout)

    def test_wrapper_rejects_current_unformatted_fixture(self) -> None:
        result = run_wrapper("changed")

        self.assertIn(result.returncode, (0, 1), msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")

    def test_invalid_native_usage_is_rejected(self) -> None:
        invalid_cases = [
            ("-i",),
            ("-i", "--dry-run", str(TEST_ROOT / OUTPUT_FIXTURE)),
            ("--style",),
            ("--files",),
            ("--files=",),
            ("--unknown",),
        ]

        for args in invalid_cases:
            with self.subTest(args=args):
                result = native_format(*args)

                self.assertEqual(2, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
                self.assertIn("Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
