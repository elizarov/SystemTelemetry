from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path
from typing import Any


TEST_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TEST_ROOT.parents[2]
REPORT_PATH = TEST_ROOT / "build" / "lint_report.json"


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

    def test_lint_check_reports_all_known_violation_shapes(self) -> None:
        shutil.rmtree(TEST_ROOT / "build", ignore_errors=True)

        env = os.environ.copy()
        env["GIT_CEILING_DIRECTORIES"] = str(TEST_ROOT.parent)
        result = subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "tools" / "lint_check.py"),
                "--config",
                str(REPO_ROOT / "tools" / "lint_config.json"),
                "--check",
                "--skip-svg",
                "--report-json",
                str(REPORT_PATH.relative_to(TEST_ROOT)),
                "--output",
                "build/source_dependencies.dot",
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
        self.assertTrue(REPORT_PATH.is_file(), "lint_check.py did not write the JSON report")

        report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
        expected = json.loads((TEST_ROOT / "lint_testcases.json").read_text(encoding="utf-8"))

        self.assertEqual(1, report["schema_version"])
        self.assertTrue(report["failed"])
        self.assertEqual(canonical_diagnostics(expected), canonical_diagnostics(report["diagnostics"]))


if __name__ == "__main__":
    unittest.main()
