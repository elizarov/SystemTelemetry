from __future__ import annotations

import json
import os
import shutil
import subprocess
import unittest
from pathlib import Path
from typing import Any


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
REPORT_PATH = TEST_ROOT / "build" / "lint_report.json"
TOOLS_EXE = REPO_ROOT / "build" / "CaseDashTools.exe"
VENDORED_TOOL_HEADER = (
    REPO_ROOT / "src" / "tools" / "vendor" / "tree-sitter" / "tree-sitter-cpp" / "src" / "tree_sitter" / "parser.h"
)


def canonical_diagnostics(diagnostics: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return sorted(
        diagnostics,
        key=lambda item: (
            str(item["check"]),
            str(item["type"]),
            str(item.get("location", "")),
            str(item.get("kind", "")),
            str(item["message"]),
        ),
    )


class LintCheckTests(unittest.TestCase):
    maxDiff = None

    def test_lint_check_excludes_tool_vendor_from_clean_summary_accounting(self) -> None:
        clean_root = TEST_ROOT / "build" / "clean_summary"
        shutil.rmtree(clean_root, ignore_errors=True)
        (clean_root / "src" / "tools" / "vendor").mkdir(parents=True)
        (clean_root / "src" / "maintained.h").write_text("#pragma once\n", encoding="utf-8")
        (clean_root / "src" / "tools" / "vendor" / "ignored.h").write_text(
            "#pragma once\n" + "std::function<void()> ignored;\n" * 25,
            encoding="utf-8",
        )

        env = os.environ.copy()
        env["GIT_CEILING_DIRECTORIES"] = str(TEST_ROOT.parent)
        result = subprocess.run(
            [
                str(TOOLS_EXE),
                "lint_check",
                "--config",
                str(REPO_ROOT / "tools" / "lint_config.json"),
                "--check",
                "--no-progress",
                "-v",
            ],
            cwd=clean_root,
            env=env,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertIn("Scanned 1 LOC across 1 lint input file(s).", result.stdout)
        self.assertIn("  1. 1 LOC: maintained -> (none)", result.stdout)
        self.assertNotIn("ignored", result.stdout)

    def test_lint_check_allows_tool_threading_primitives(self) -> None:
        clean_root = TEST_ROOT / "build" / "tools_threading"
        shutil.rmtree(clean_root, ignore_errors=True)
        (clean_root / "src" / "tools").mkdir(parents=True)
        (clean_root / "src" / "tools" / "threaded.h").write_text(
            "#pragma once\n"
            "\n"
            "#include <thread>\n"
            "\n"
            "struct ToolThreaded {\n"
            "    std::thread worker;\n"
            "};\n",
            encoding="utf-8",
        )
        (clean_root / "src" / "tools" / "threaded.cpp").write_text(
            '#include "tools/threaded.h"\n',
            encoding="utf-8",
        )

        env = os.environ.copy()
        env["GIT_CEILING_DIRECTORIES"] = str(TEST_ROOT.parent)
        result = subprocess.run(
            [
                str(TOOLS_EXE),
                "lint_check",
                "--config",
                str(REPO_ROOT / "tools" / "lint_config.json"),
                "--check",
                "--no-progress",
                "--concurrency",
                "1",
            ],
            cwd=clean_root,
            env=env,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(0, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertIn("Scanned 8 LOC across 2 lint input file(s).", result.stdout)

    def test_lint_check_reports_all_known_violation_shapes(self) -> None:
        shutil.rmtree(TEST_ROOT / "build", ignore_errors=True)

        env = os.environ.copy()
        env["GIT_CEILING_DIRECTORIES"] = str(TEST_ROOT.parent)
        result = subprocess.run(
            [
                str(TOOLS_EXE),
                "lint_check",
                "--config",
                str(REPO_ROOT / "tools" / "lint_config.json"),
                "--check",
                "--report-json",
                str(REPORT_PATH.relative_to(TEST_ROOT)),
            ],
            cwd=TEST_ROOT,
            env=env,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            1,
            result.returncode,
            msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}",
        )
        self.assertTrue(REPORT_PATH.is_file(), "CaseDashTools lint_check did not write the JSON report")

        report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
        expected = json.loads((TEST_ROOT / "lint_testcases.json").read_text(encoding="utf-8"))

        self.assertEqual(1, report["schema_version"])
        self.assertTrue(report["failed"])
        self.assertEqual(canonical_diagnostics(expected), canonical_diagnostics(report["diagnostics"]))

    def test_lint_tool_freshness_ignores_vendored_tool_sources(self) -> None:
        original_stat = VENDORED_TOOL_HEADER.stat()
        future_time = max(original_stat.st_mtime, TOOLS_EXE.stat().st_mtime + 5.0)

        try:
            os.utime(VENDORED_TOOL_HEADER, (future_time, future_time))
            env = os.environ.copy()
            env["GIT_CEILING_DIRECTORIES"] = str(TEST_ROOT.parent)
            result = subprocess.run(
                [
                    str(TOOLS_EXE),
                    "lint_check",
                    "--config",
                    str(REPO_ROOT / "tools" / "lint_config.json"),
                    "--check",
                    "--no-progress",
                ],
                cwd=TEST_ROOT,
                env=env,
                check=False,
                capture_output=True,
                text=True,
            )
        finally:
            os.utime(VENDORED_TOOL_HEADER, (original_stat.st_atime, original_stat.st_mtime))

        self.assertEqual(1, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")
        self.assertNotIn("CaseDashTools build inputs changed", result.stderr)

    def test_lint_check_rejects_removed_graph_arguments(self) -> None:
        result = subprocess.run(
            [
                str(TOOLS_EXE),
                "lint_check",
                "--skip-svg",
            ],
            cwd=TEST_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(2, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")

    def test_lint_check_rejects_invalid_concurrency(self) -> None:
        for args in [("--concurrency",), ("--concurrency", "0"), ("--concurrency", "nope")]:
            with self.subTest(args=args):
                result = subprocess.run(
                    [
                        str(TOOLS_EXE),
                        "lint_check",
                        *args,
                    ],
                    cwd=TEST_ROOT,
                    check=False,
                    capture_output=True,
                    text=True,
                )

                self.assertEqual(2, result.returncode, msg=f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}")


if __name__ == "__main__":
    unittest.main()
