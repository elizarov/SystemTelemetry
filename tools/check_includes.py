#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
import re


PROJECT_ROOT = Path(__file__).resolve().parents[1]
INCLUDE_RE = re.compile(r'^\s*#include\s*"([^"]+)"')
CHECKED_SUFFIXES = {".h", ".cpp", ".rc"}
SCANNED_ROOTS = ("src", "tests", "resources")
EXCLUDED_PREFIXES = ("src/vendor/",)
INCLUDE_ROOTS = (
    ("src", PROJECT_ROOT / "src"),
    ("resources", PROJECT_ROOT / "resources"),
)


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
        result = subprocess.run(
            ["git", "ls-files", *SCANNED_ROOTS],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        files = [PROJECT_ROOT / line.strip() for line in result.stdout.splitlines() if line.strip()]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for root in SCANNED_ROOTS:
            files.extend((PROJECT_ROOT / root).rglob("*"))
    return [path for path in files if path.is_file() and is_checked_file(path)]


def normalize_include(include_text: str) -> str:
    return include_text.replace("\\", "/")


def resolve_project_include(current_file: Path, include_text: str) -> tuple[Path, str] | None:
    include_path = Path(normalize_include(include_text))
    candidates = [current_file.parent / include_path]
    candidates.extend(root / include_path for _, root in INCLUDE_ROOTS)

    for candidate in candidates:
        if not candidate.is_file():
            continue
        candidate_resolved = candidate.resolve()
        for root_name, root_path in INCLUDE_ROOTS:
            try:
                candidate_resolved.relative_to(root_path.resolve())
            except ValueError:
                continue
            candidate_rel = relpath(candidate_resolved)
            if any(candidate_rel.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
                return None
            return candidate_resolved, root_name
    return None


def expected_include_for(path: Path, root_name: str) -> str:
    for candidate_root_name, root_path in INCLUDE_ROOTS:
        if candidate_root_name != root_name:
            continue
        return path.relative_to(root_path.resolve()).as_posix()
    raise ValueError(f"Unknown include root {root_name}")


def collect_violations(files: list[Path]) -> list[Violation]:
    violations: list[Violation] = []
    for path in files:
        text = path.read_text(encoding="utf-8")
        file_rel = relpath(path)
        for line_number, line in enumerate(text.splitlines(), start=1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            include_text = match.group(1)
            resolved = resolve_project_include(path, include_text)
            if resolved is None:
                continue
            target_path, root_name = resolved
            expected = expected_include_for(target_path, root_name)
            normalized = normalize_include(include_text)
            if normalized == expected:
                continue
            violations.append(
                Violation(
                    relpath=file_rel,
                    line=line_number,
                    message=(
                        f'Project header "{include_text}" resolves to {relpath(target_path)}; '
                        f'use "{expected}" from the {root_name} include root instead of a relative or local shorthand path.'
                    ),
                )
            )
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="Check project include paths use src/resources include roots.")
    parser.parse_args()

    files = sorted(list_project_files())
    violations = sorted(collect_violations(files), key=lambda item: (item.relpath, item.line, item.message))

    if violations:
        for violation in violations:
            print(f"{violation.relpath}:{violation.line}: include-style: {violation.message}")
        print(f"\nInclude style check failed with {len(violations)} violation(s).")
        return 1

    print(f"Include style check passed for {len(files)} tracked source, test, and resource files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
