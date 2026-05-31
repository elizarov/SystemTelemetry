from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
FORMAT_CMD = REPO_ROOT / "format.cmd"
FORMAT_EXE = REPO_ROOT / "build" / "CaseDashTools.exe"
INPUT_FIXTURE = Path("src") / "format_test_input.cpp"
OUTPUT_FIXTURE = Path("src") / "format_test_output.cpp"
USERVER_INPUT_FIXTURE = Path("src") / "format_userver_input.cpp"
USERVER_OUTPUT_FIXTURE = Path("src") / "format_userver_output.cpp"
USERVER_FORMAT_CONFIG = TEST_ROOT / ".cpp-format-userver"


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


@contextmanager
def copied_fixtures(*paths: Path):
    build_dir = REPO_ROOT / "build"
    build_dir.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="format_fixtures_", dir=build_dir) as temp_dir:
        root = Path(temp_dir)
        copies = {}
        for path in paths:
            copied_path = root / path.name
            shutil.copyfile(TEST_ROOT / path, copied_path)
            copies[path] = copied_path
        yield copies


class FormatCommandTests(unittest.TestCase):
    maxDiff = None

    def test_stdin_formats_to_expected_output(self) -> None:
        result = native_format("--style=file", cwd=TEST_ROOT, input_text=read_fixture(INPUT_FIXTURE))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(OUTPUT_FIXTURE), result.stdout)
        self.assertRegex(result.stderr, r"Formatted stdin in (?:\d+ms|\d+\.\d{3}s)\.\s*$")

    def test_golden_input_parses_without_errors(self) -> None:
        with copied_fixtures(INPUT_FIXTURE) as fixtures:
            result = native_format("--style=file", str(fixtures[INPUT_FIXTURE]))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertNotIn("tree-sitter parse failed", result.stderr)

    def test_userver_stdin_formats_to_expected_output(self) -> None:
        result = native_format(
            f"--style={USERVER_FORMAT_CONFIG}",
            cwd=TEST_ROOT,
            input_text=read_fixture(USERVER_INPUT_FIXTURE),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(USERVER_OUTPUT_FIXTURE), result.stdout)
        self.assertRegex(result.stderr, r"Formatted stdin in (?:\d+ms|\d+\.\d{3}s)\.\s*$")

    def test_userver_golden_input_parses_without_errors(self) -> None:
        with copied_fixtures(USERVER_INPUT_FIXTURE) as fixtures:
            result = native_format(f"--style={USERVER_FORMAT_CONFIG}", str(fixtures[USERVER_INPUT_FIXTURE]))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertNotIn("tree-sitter parse failed", result.stderr)

    def test_missing_include_categories_preserves_opening_include_blocks(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_preserve_includes_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            config = root / ".cpp-format"
            config.write_text("---\nColumnLimit: 120\nIndentWidth: 4\nTabWidth: 4\n", encoding="utf-8")
            cases = [
                (
                    "#pragma once\n\n"
                    "#include <zeta>\n"
                    "#include <alpha>\n\n"
                    "#include \"b.h\"\n"
                    "#include \"a.h\"\n\n"
                    "int value;\n"
                ),
                (
                    "#ifndef PRESERVE_FIXTURE_HPP\n"
                    "#define PRESERVE_FIXTURE_HPP\n\n"
                    "#include <zeta>\n"
                    "#include <alpha>\n\n"
                    "#include \"b.h\"\n"
                    "#include \"a.h\"\n\n"
                    "int value;\n\n"
                    "#endif\n"
                ),
            ]
            for text in cases:
                with self.subTest(text=text.splitlines()[0]):
                    result = native_format(f"--style={config}", input_text=text)

                    self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
                    self.assertEqual(text, result.stdout)

    def test_include_categories_sort_opening_include_blocks(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_sort_includes_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            config = root / ".cpp-format"
            config.write_text(
                "---\n"
                "ColumnLimit: 120\n"
                "IndentWidth: 4\n"
                "TabWidth: 4\n"
                "IncludeCategories:\n"
                "  - Regex: '^<.*>$'\n"
                "    Priority: 1\n"
                "  - Regex: '^\".*\"$'\n"
                "    Priority: 2\n",
                encoding="utf-8",
            )
            input_text = (
                "#ifndef SORT_FIXTURE_HPP\n"
                "#define SORT_FIXTURE_HPP\n\n"
                "#include \"b.h\"\n"
                "#include <zeta>\n\n"
                "#include \"a.h\"\n"
                "#include <alpha>\n\n"
                "int value;\n\n"
                "#endif\n"
            )
            result = native_format(f"--style={config}", input_text=input_text)

            self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertEqual(
                "#ifndef SORT_FIXTURE_HPP\n"
                "#define SORT_FIXTURE_HPP\n\n"
                "#include <alpha>\n"
                "#include <zeta>\n\n"
                "#include \"a.h\"\n"
                "#include \"b.h\"\n\n"
                "int value;\n\n"
                "#endif\n",
                result.stdout,
            )

    def test_file_argument_formats_to_stdout(self) -> None:
        with copied_fixtures(OUTPUT_FIXTURE) as fixtures:
            result = native_format("--style=file", str(fixtures[OUTPUT_FIXTURE]))

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(read_fixture(OUTPUT_FIXTURE), result.stdout)
        self.assertRegex(
            result.stderr,
            r"Formatted 1 file in (?:\d+ms|\d+\.\d{3}s)\.\s*$",
        )

    def test_dry_run_accepts_idempotent_file_and_rejects_unformatted_file(self) -> None:
        with copied_fixtures(INPUT_FIXTURE, OUTPUT_FIXTURE) as fixtures:
            ok_result = native_format("--style=file", "--dry-run", str(fixtures[OUTPUT_FIXTURE]))

            self.assertEqual(0, ok_result.returncode, msg=f"stdout:\n{ok_result.stdout}\n\nstderr:\n{ok_result.stderr}")
            self.assertRegex(
                ok_result.stdout,
                r"Checked 1 file in (?:\d+ms|\d+\.\d{3}s)\.\s*$",
            )

            bad_result = native_format("--style=file", "--dry-run", str(fixtures[INPUT_FIXTURE]))

            self.assertEqual(1, bad_result.returncode, msg=f"stdout:\n{bad_result.stdout}\n\nstderr:\n{bad_result.stderr}")
            self.assertIn("Formatting is required for 1 file", bad_result.stdout)

    def test_files_option_reads_newline_file_list(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_files_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            source = root / OUTPUT_FIXTURE.name
            shutil.copyfile(TEST_ROOT / OUTPUT_FIXTURE, source)
            file_list = root / "files.txt"
            file_list.write_text(f"{source}\n\n", encoding="utf-8")

            result = native_format("--style=file", "--dry-run", "--concurrency", "1", "--files", str(file_list))

            self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertRegex(result.stdout, r"Checked 1 file in (?:\d+ms|\d+\.\d{3}s)\.\s*$")

    def test_concurrency_one_preserves_file_list_output_order(self) -> None:
        build_dir = REPO_ROOT / "build"
        build_dir.mkdir(exist_ok=True)

        with tempfile.TemporaryDirectory(prefix="format_order_", dir=build_dir) as temp_dir:
            root = Path(temp_dir)
            shutil.copyfile(REPO_ROOT / ".cpp-format", root / ".cpp-format")
            first = root / "first.cpp"
            second = root / "second.cpp"
            first.write_text("int first(){return 1;}\n", encoding="utf-8")
            second.write_text("int second(){return 2;}\n", encoding="utf-8")
            file_list = root / "files.txt"
            file_list.write_text(f"{second}\n{first}\n", encoding="utf-8")

            result = native_format("--style=file", "--concurrency", "1", "--files", str(file_list), cwd=root)

            self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
            self.assertEqual(
                "int second() {\n"
                "    return 2;\n"
                "}\n"
                "int first() {\n"
                "    return 1;\n"
                "}\n",
                result.stdout,
            )
            self.assertRegex(result.stderr, r"Formatted 2 files in (?:\d+ms|\d+\.\d{3}s)\.\s*$")

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
            "void f(Object^ handle, Object% tracking, int&& moved, int* pointer) {}\n",
            result.stdout,
        )

    def test_trailing_comma_normalization(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "enum E { A, B };\n"
                "enum F { C, D, };\n"
                "int values[] = {1, 2,};\n"
                "void f(){ Use({1, 2,}); }\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "enum E {\n"
            "    A,\n"
            "    B,\n"
            "};\n"
            "enum F {\n"
            "    C,\n"
            "    D,\n"
            "};\n"
            "int values[] = {1, 2};\n"
            "void f() {\n"
            "    Use({1, 2});\n"
            "}\n",
            result.stdout,
        )

    def test_final_undef_does_not_emit_trailing_empty_line(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "#define VALUE 1\n"
                "int value;\n"
                "#undef VALUE\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "#define VALUE 1\n"
            "\n"
            "int value;\n"
            "\n"
            "#undef VALUE\n",
            result.stdout,
        )

    def test_win32_boolean_macros_preserve_spelling(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "int false_value=FALSE;\n"
                "int true_value=TRUE;\n"
                "bool standard_false=false;\n"
                "bool standard_true=true;\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "int false_value = FALSE;\n"
            "int true_value = TRUE;\n"
            "bool standard_false = false;\n"
            "bool standard_true = true;\n",
            result.stdout,
        )

    def test_macro_decltype_argument_is_preserved(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "#define CASEDASH_LOAD_OPTIONAL(function, name) \\\n"
                "function=reinterpret_cast<decltype(function)>(GetProcAddress(module_,name))\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "#define CASEDASH_LOAD_OPTIONAL(function, name) \\\n"
            "    function = reinterpret_cast<decltype(function)>(GetProcAddress(module_, name))\n",
            result.stdout,
        )

    def test_compact_empty_brace_ternary_colon_keeps_space(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "auto snapshot=preferred?TreeViewportSnapshot{}:CaptureTreeViewportSnapshot();\n"
                "auto text=empty?std::string{}:value;\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "auto snapshot = preferred ? TreeViewportSnapshot{} : CaptureTreeViewportSnapshot();\n"
            "auto text = empty ? std::string{} : value;\n",
            result.stdout,
        )

    def test_compact_initializer_braces_stay_tight_in_split_context(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "const auto matchesDrag = [&](const LayoutEditOverlayOwner& owner) {\n"
                "return owner.childIndex==drag.currentIndex&&\n"
                "MatchesLayoutContainerEditKey(LayoutContainerEditKey{owner.key.editCardId,owner.key.nodePath},\n"
                "LayoutContainerEditKey{drag.key.editCardId,drag.key.nodePath});\n"
                "};\n"
                "bool hits(){return MatchesRegionHit(regions,region,RenderPoint{x,y})&&\n"
                "MatchesRegionHit(regions,region,RenderPoint{x+3,y});}\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "const auto matchesDrag = [&](const LayoutEditOverlayOwner& owner) {\n"
            "    return owner.childIndex == drag.currentIndex && MatchesLayoutContainerEditKey(\n"
            "        LayoutContainerEditKey{owner.key.editCardId, owner.key.nodePath},\n"
            "        LayoutContainerEditKey{drag.key.editCardId, drag.key.nodePath}\n"
            "    );\n"
            "};\n"
            "bool hits() {\n"
            "    return MatchesRegionHit(regions, region, RenderPoint{x, y}) &&\n"
            "        MatchesRegionHit(regions, region, RenderPoint{x + 3, y});\n"
            "}\n",
            result.stdout,
        )

    def test_control_body_brace_normalization(self) -> None:
        result = native_format(
            "--style=file",
            input_text=(
                "void f(int* values,int count){\n"
                "if(count) values[0]+=1;\n"
                "else values[0]=0;\n"
                "if(count==0) values[0]=0;\n"
                "else if(count==1) values[0]=1;\n"
                "else values[0]=2;\n"
                "while(count) --count;\n"
                "for(int i=0;i<count;++i) values[i]+=i;\n"
                "do ++count; while(count<10);\n"
                "if(count) { return; } else { if(count) return; }\n"
                "}\n"
            ),
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "void f(int* values, int count) {\n"
            "    if (count) {\n"
            "        values[0] += 1;\n"
            "    } else {\n"
            "        values[0] = 0;\n"
            "    }\n"
            "    if (count == 0) {\n"
            "        values[0] = 0;\n"
            "    } else if (count == 1) {\n"
            "        values[0] = 1;\n"
            "    } else {\n"
            "        values[0] = 2;\n"
            "    }\n"
            "    while (count) {\n"
            "        --count;\n"
            "    }\n"
            "    for (int i = 0; i < count; ++i) {\n"
            "        values[i] += i;\n"
            "    }\n"
            "    do {\n"
            "        ++count;\n"
            "    } while (count < 10);\n"
            "    if (count) {\n"
            "        return;\n"
            "    } else if (count) {\n"
            "        return;\n"
            "    }\n"
            "}\n",
            result.stdout,
        )

    def test_lambda_argument_and_split_function_parameters_are_allowed(self) -> None:
        input_text = (
            "struct IncludeGroup { int priority; };\n"
            "void SortIncludeGroups(std::vector<IncludeGroup>& groups) {\n"
            "    std::sort(groups.begin(), groups.end(), [](const IncludeGroup& left, const IncludeGroup& right) {\n"
            "        return left.priority < right.priority;\n"
            "    });\n"
            "}\n"
            "\n"
            "std::set<std::string> RequireSuffixGroup(\n"
            "    const std::map<std::string, std::set<std::string>>& suffixGroups,\n"
            "    std::string_view configPath,\n"
            "    std::string_view groupName\n"
            ") {\n"
            "   return {};\n"
            "}\n"
        )
        result = native_format("--style=file", input_text=input_text)

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertEqual(
            "struct IncludeGroup {\n"
            "    int priority;\n"
            "};\n"
            "void SortIncludeGroups(std::vector<IncludeGroup>& groups) {\n"
            "    std::sort(groups.begin(), groups.end(), [](const IncludeGroup& left, const IncludeGroup& right) {\n"
            "        return left.priority < right.priority;\n"
            "    });\n"
            "}\n"
            "\n"
            "std::set<std::string> RequireSuffixGroup(\n"
            "    const std::map<std::string, std::set<std::string>>& suffixGroups,\n"
            "    std::string_view configPath,\n"
            "    std::string_view groupName\n"
            ") {\n"
            "    return {};\n"
            "}\n",
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
            ("--concurrency",),
            ("--concurrency", "0"),
            ("--concurrency", "nope"),
            ("--unknown",),
        ]

        for args in invalid_cases:
            with self.subTest(args=args):
                result = native_format(*args)

                self.assertEqual(2, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
                self.assertIn("Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
