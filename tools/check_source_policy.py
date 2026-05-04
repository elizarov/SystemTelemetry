#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SCANNED_ROOTS = ("src", "tests")
CHECKED_SUFFIXES = {".h", ".cpp"}
EXCLUDED_PREFIXES = ("src/vendor/",)
STD_FUNCTION_RE = re.compile(r"\bstd\s*::\s*function\b")
STD_FILESYSTEM_RE = re.compile(r"\bstd\s*::\s*filesystem\b")
FILESYSTEM_INCLUDE_RE = re.compile(r"^\s*#\s*include\s*<\s*filesystem\s*>")


@dataclass(frozen=True)
class Violation:
    relpath: str
    line: int
    message: str


def relpath(path: Path) -> str:
    return path.relative_to(PROJECT_ROOT).as_posix()


def is_checked_file(path: Path) -> bool:
    relative = relpath(path)
    if path.suffix not in CHECKED_SUFFIXES:
        return False
    if not any(relative.startswith(f"{root}/") for root in SCANNED_ROOTS):
        return False
    return not any(relative.startswith(prefix) for prefix in EXCLUDED_PREFIXES)


def list_project_files() -> list[Path]:
    try:
        tracked = subprocess.run(
            ["git", "ls-files", *SCANNED_ROOTS],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        untracked = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard", *SCANNED_ROOTS],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        paths = tracked.stdout.splitlines() + untracked.stdout.splitlines()
        files = [PROJECT_ROOT / line.strip() for line in paths if line.strip()]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for root in SCANNED_ROOTS:
            files.extend((PROJECT_ROOT / root).rglob("*"))
    return [path for path in files if path.is_file() and is_checked_file(path)]


def strip_comments_and_strings(text: str) -> str:
    result: list[str] = []
    i = 0
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
                result.append("\n")
            else:
                result.append(" ")
            i += 1
            continue
        if in_block_comment:
            if ch == "*" and nxt == "/":
                result.extend("  ")
                in_block_comment = False
                i += 2
            else:
                result.append("\n" if ch == "\n" else " ")
                i += 1
            continue
        if in_string:
            if ch == "\\" and nxt:
                result.extend("  ")
                i += 2
                continue
            result.append("\n" if ch == "\n" else " ")
            if ch == '"':
                in_string = False
            i += 1
            continue
        if in_char:
            if ch == "\\" and nxt:
                result.extend("  ")
                i += 2
                continue
            result.append("\n" if ch == "\n" else " ")
            if ch == "'":
                in_char = False
            i += 1
            continue
        if ch == "/" and nxt == "/":
            result.extend("  ")
            in_line_comment = True
            i += 2
            continue
        if ch == "/" and nxt == "*":
            result.extend("  ")
            in_block_comment = True
            i += 2
            continue
        if ch == '"':
            result.append(" ")
            in_string = True
            i += 1
            continue
        if ch == "'":
            result.append(" ")
            in_char = True
            i += 1
            continue
        result.append(ch)
        i += 1
    return "".join(result)


def collect_violations(files: list[Path]) -> list[Violation]:
    violations: list[Violation] = []
    for path in files:
        stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        file_rel = relpath(path)
        for line_number, line in enumerate(stripped.splitlines(), start=1):
            if STD_FUNCTION_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "std::function is not allowed in maintained source; use FunctionRef for synchronous "
                            "borrowed callbacks or a purpose-built interface when ownership must escape the call."
                        ),
                    )
                )
            if STD_FILESYSTEM_RE.search(line) or FILESYSTEM_INCLUDE_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "std::filesystem is not allowed in maintained source; use src/util/file_path.* helpers "
                            "so path handling stays Win32-backed without pulling filesystem machinery into the app."
                        ),
                    )
                )
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="Check project source policy rules.")
    parser.parse_args()

    files = sorted(list_project_files())
    violations = sorted(collect_violations(files), key=lambda item: (item.relpath, item.line, item.message))
    if violations:
        for violation in violations:
            print(f"{violation.relpath}:{violation.line}: source-policy: {violation.message}")
        print(f"\nSource policy check failed with {len(violations)} violation(s).")
        return 1

    print(f"Source policy check passed for {len(files)} tracked source and test files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
