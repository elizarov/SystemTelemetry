from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
FORMAT_CMD = REPO_ROOT / "format.cmd"
TOOLS_EXE = REPO_ROOT / "build" / "CaseDashTools.exe"
CLANG_FORMAT_CONFIG = REPO_ROOT / ".clang-format"
NATIVE_FORMAT_CONFIG = REPO_ROOT / "tools" / "format_config.json"
BAD_FIXTURE = Path("src") / "format_options.cpp"
GOLDEN_FIXTURE = Path("src") / "format_options.golden.cpp"

FORMAT_OPTION_COVERAGE = {
    "BasedOnStyle": "fixture validates the project overrides from the LLVM C++ baseline",
    "Language": "fixture is a C++ translation unit",
    "UseTab": "bad fixture uses tab indentation and golden output uses spaces",
    "IndentWidth": "namespace, class, control, and block bodies use four-space indentation",
    "TabWidth": "tab-indented bad fixture normalizes to the same four-space grid",
    "ContinuationIndentWidth": "wrapped parameters, arguments, and conditions use four-space continuation indentation",
    "ColumnLimit": "long declarations, calls, and comments exercise wrapping decisions",
    "BreakBeforeBraces": "namespace, class, function, loop, and switch braces attach",
    "BreakBeforeTernaryOperators": "long and nested ternary expressions break as compact branches without padded operator columns",
    "NamespaceIndentation": "namespace contents are not indented",
    "IndentCaseLabels": "switch case labels are indented inside the switch block",
    "IndentAccessModifiers": "class access labels stay outdented from members",
    "AccessModifierOffset": "class access labels use the configured negative offset",
    "PointerAlignment": "pointer declarations and parameters bind the star to the type",
    "ReferenceAlignment": "reference declarations and parameters bind the ampersand to the type",
    "SpaceBeforeParens": "control statements gain a space and function declarations do not",
    "AlignAfterOpenBracket": "wrapped braced lists, parameters, and arguments use continuation indentation instead of column alignment",
    "AlignArrayOfStructures": "table rows are not column-aligned",
    "AlignConsecutiveAssignments": "consecutive assignments keep ordinary spacing",
    "AlignConsecutiveBitFields": "bitfield colons are not column-aligned",
    "AlignConsecutiveDeclarations": "consecutive declarations are not column-aligned",
    "AlignConsecutiveMacros": "consecutive macro definitions are not column-aligned",
    "AlignConsecutiveShortCaseStatements": "short case labels do not use case-column alignment",
    "AlignEscapedNewlines": "macro continuation backslashes are not right-aligned",
    "AlignOperands": "wrapped boolean operands use continuation indentation instead of operand alignment",
    "AlignTrailingComments": "trailing comments are not column-aligned",
    "BinPackArguments": "long call arguments wrap onto separate continuation lines",
    "BinPackParameters": "long function parameters wrap onto separate continuation lines",
    "BreakConstructorInitializers": "constructor initializer lists break after the colon",
    "PackConstructorInitializers": "constructor initializer lists keep one initializer per continuation line",
    "PenaltyIndentedWhitespace": "long ternary expressions prefer compact continuation breaks over horizontal alignment",
    "AllowShortBlocksOnASingleLine": "short loop blocks expand across multiple lines",
    "AllowShortCaseLabelsOnASingleLine": "short case labels expand across multiple lines",
    "AllowShortFunctionsOnASingleLine": "non-empty short functions expand and empty functions stay single-line",
    "AllowShortIfStatementsOnASingleLine": "short if statements expand across multiple lines",
    "AllowShortLoopsOnASingleLine": "short while loops expand across multiple lines",
    "IncludeBlocks": "mixed include blocks regroup into configured include categories",
    "IncludeCategories": "WinSock, Windows, system, vendor, and project includes are separated",
    "MainIncludeChar": "the matching quoted header stays in the main-include position",
    "IncludeIsMainRegex": "format_options.h is recognized as the main include for format_options.cpp",
    "SortIncludes": "system and project includes sort case-insensitively",
    "SeparateDefinitionBlocks": "function definitions are separated by blank lines",
    "ReflowComments": "the over-limit comment remains on one physical line",
    "SpacesBeforeTrailingComments": "the trailing comment is separated by two spaces",
    "PenaltyReturnTypeOnItsOwnLine": "long method declarations keep the return type with the function name",
}


def run_format(*args: str) -> subprocess.CompletedProcess[str]:
    command = subprocess.list2cmdline([str(FORMAT_CMD), "--root", str(TEST_ROOT), *args])
    return subprocess.run(
        ["cmd.exe", "/d", "/c", command],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def run_native_format(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(TOOLS_EXE), "format", "--root", str(root), *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def read_fixture(path: Path) -> str:
    return (TEST_ROOT / path).read_text(encoding="utf-8")


def configured_format_options() -> set[str]:
    options: set[str] = set()
    option_pattern = re.compile(r"^([A-Za-z][A-Za-z0-9_]*):")

    for line in CLANG_FORMAT_CONFIG.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped == "---" or stripped.startswith("#"):
            continue
        if line[:1].isspace() or stripped.startswith("- "):
            continue

        match = option_pattern.match(line)
        if match:
            options.add(match.group(1))

    return options


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
            r"Checked 1 all file in (?:\d+ms|\d+\.\d{3}s)\. Formatting is up to date\.\s*$",
        )

    def test_golden_fixture_uses_project_indentation_constraints(self) -> None:
        golden_text = read_fixture(GOLDEN_FIXTURE)

        self.assertNotRegex(golden_text, r"\S {2,}\?")
        self.assertIn('return isFontsSection ? "Revert Font Changes" :', golden_text)
        self.assertNotIn("return isFontsSection ?\n", golden_text)
        for line_number, line in enumerate(golden_text.splitlines(), start=1):
            match = re.match(r"^( +)\S", line)
            if match:
                self.assertEqual(0, len(match.group(1)) % 4, msg=f"line {line_number}: {line}")

    def test_bad_fixture_is_rejected_by_check_mode(self) -> None:
        result = run_format("--file", str(BAD_FIXTURE))

        self.assertEqual(1, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertIn("Formatting is required.", result.stdout)

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

    def test_fixture_covers_configured_clang_format_options(self) -> None:
        self.assertSetEqual(set(FORMAT_OPTION_COVERAGE), configured_format_options())

    def test_native_formatter_uses_greedy_wrapping_and_comment_forced_splits(self) -> None:
        with tempfile.TemporaryDirectory(dir=REPO_ROOT / "build") as temp_dir:
            root = Path(temp_dir)
            (root / "src").mkdir()
            (root / "tools").mkdir()
            shutil.copy2(NATIVE_FORMAT_CONFIG, root / "tools" / "format_config.json")
            source = root / "src" / "sample.cpp"
            source.write_text(
                textwrap.dedent(
                    """
                    #include <vector>
                    #include "zeta.h"
                    #include <windows.h>
                    #include "sample.h"
                    #include <winsock2.h>

                    void f(){auto value=buildValue(firstValueWithQuiteLongName,transform(secondValueWithQuiteLongNameA,secondValueWithQuiteLongNameB),thirdValueWithQuiteLongName); update(firstValue,secondValue, // note
                    thirdValue); int sum=firstValue+secondValue+ // keep
                    thirdValue;}
                    """
                ).lstrip(),
                encoding="utf-8",
            )

            result = run_native_format(root, "--file", "src/sample.cpp", "--stdout")

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            textwrap.dedent(
                """
                #include "sample.h"

                #include <winsock2.h>

                #include <windows.h>

                #include <vector>

                #include "zeta.h"

                void f() {
                    auto value =
                        buildValue(
                            firstValueWithQuiteLongName,
                            transform(secondValueWithQuiteLongNameA, secondValueWithQuiteLongNameB),
                            thirdValueWithQuiteLongName
                        );
                    update(
                        firstValue,
                        secondValue,  // note
                        thirdValue
                    );
                    int sum =
                        firstValue +
                        secondValue +  // keep
                        thirdValue;
                }
                """
            ).lstrip(),
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
