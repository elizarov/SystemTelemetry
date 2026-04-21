#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = PROJECT_ROOT / "src"
EXCLUDED_PREFIXES = ("src/vendor/",)
HEADER_BODY_ALLOWLIST = {
    "src/config_schema.h",
}
CPP_WITHOUT_HEADER_ALLOWLIST = {
    "src/app_main.cpp",
    "src/board_gigabyte_siv.cpp",
    "src/gpu_amd_adl.cpp",
    "src/telemetry_runtime_fake.cpp",
}
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch"}
CLASS_DECL_RE = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\b")
QUALIFIED_DEF_RE = re.compile(
    r"(?:^|[\s*&])([A-Za-z_]\w*(?:::[A-Za-z_~]\w*)+)\s*\([^;{}()]*\)\s*(?:const\b)?\s*(?:noexcept\b)?(?:\s*->\s*[^{}]+)?$"
)
FREE_DECL_RE = re.compile(
    r"(?:^|[\s*&])([A-Za-z_]\w*)\s*\([^;{}()]*\)\s*(?:const\b)?\s*(?:noexcept\b)?(?:\s*->\s*[^{}]+)?$"
)
HEADER_BODY_RE = re.compile(
    r"(?:^|[\s*&])([A-Za-z_~]\w*(?:::[A-Za-z_~]\w*)*)\s*\([^;{}()]*\)\s*(?:const\b)?\s*(?:noexcept\b)?(?:\s*->\s*[^{}]+)?\s*\{",
    re.MULTILINE,
)


@dataclass(frozen=True)
class Violation:
    kind: str
    relpath: str
    line: int
    message: str


def relpath(path: Path) -> str:
    return path.relative_to(PROJECT_ROOT).as_posix()


def is_project_source(path: Path) -> bool:
    relative = relpath(path)
    if not relative.startswith("src/"):
        return False
    return not any(relative.startswith(prefix) for prefix in EXCLUDED_PREFIXES)


def list_project_files() -> list[Path]:
    try:
        tracked = subprocess.run(
            ["git", "ls-files", "src"],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        untracked = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard", "src"],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        paths = tracked.stdout.splitlines() + untracked.stdout.splitlines()
        files = [PROJECT_ROOT / line.strip() for line in paths if line.strip()]
    except (OSError, subprocess.CalledProcessError):
        files = list(SOURCE_ROOT.rglob("*"))
    return [path for path in files if path.is_file() and is_project_source(path)]


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


def collect_top_level_statements(stripped_text: str) -> list[tuple[int, str, str]]:
    statements: list[tuple[int, str, str]] = []
    depth = 0
    buffer: list[str] = []
    start_line = 0
    for line_number, raw_line in enumerate(stripped_text.splitlines(), start=1):
        line = raw_line.strip()
        if depth == 0 and line and not line.startswith("#"):
            if not buffer:
                start_line = line_number
            buffer.append(line)
            if ";" in line or "{" in line:
                statement = " ".join(buffer)
                brace_index = statement.find("{")
                semi_index = statement.find(";")
                if brace_index != -1 and (semi_index == -1 or brace_index < semi_index):
                    statements.append((start_line, statement.split("{", 1)[0].strip(), "{"))
                elif semi_index != -1:
                    statements.append((start_line, statement.split(";", 1)[0].strip(), ";"))
                buffer.clear()
        opens = raw_line.count("{")
        closes = raw_line.count("}")
        depth += opens - closes
        if depth < 0:
            depth = 0
    return statements


def paired_cpp_for_header(header_rel: str) -> str:
    return header_rel[:-2] + ".cpp"


def paired_header_for_cpp(cpp_rel: str) -> str:
    return cpp_rel[:-4] + ".h"


def namespace_stack_for_line(line: str, namespace_stack: list[str]) -> list[str]:
    namespace_match = re.fullmatch(r"\s*(?:inline\s+)?namespace\s+([A-Za-z_]\w*)\s*\{\s*", line)
    if namespace_match:
        namespace_stack.append(namespace_match.group(1))
        return namespace_stack
    if re.fullmatch(r"\s*}\s*(?://.*)?", line) and namespace_stack:
        namespace_stack.pop()
    return namespace_stack


def collect_class_headers(headers: Iterable[Path]) -> dict[str, set[str]]:
    mapping: dict[str, set[str]] = {}
    for path in headers:
        namespace_stack: list[str] = []
        stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        for raw_line in stripped.splitlines():
            namespace_stack_for_line(raw_line, namespace_stack)
            for match in CLASS_DECL_RE.finditer(raw_line):
                name = match.group(1)
                suffix = raw_line[match.end() :]
                if "{" not in suffix:
                    continue
                mapping.setdefault(name, set()).add(relpath(path))
                if namespace_stack:
                    mapping.setdefault("::".join([*namespace_stack, name]), set()).add(relpath(path))
    return mapping


def collect_free_function_headers(headers: Iterable[Path]) -> dict[str, set[str]]:
    mapping: dict[str, set[str]] = {}
    for path in headers:
        header_rel = relpath(path)
        stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        for line_number, statement, terminator in collect_top_level_statements(stripped):
            if terminator != ";":
                continue
            if statement.startswith(("class ", "struct ", "enum ", "using ", "typedef ")):
                continue
            if "operator" in statement:
                continue
            match = FREE_DECL_RE.search(statement)
            if not match:
                continue
            name = match.group(1)
            if name in CONTROL_KEYWORDS:
                continue
            mapping.setdefault(name, set()).add(header_rel)
    return mapping


def collect_cpp_definition_violations(
    cpps: Iterable[Path], class_headers: dict[str, set[str]], free_headers: dict[str, set[str]]
) -> list[Violation]:
    violations: list[Violation] = []
    for path in cpps:
        cpp_rel = relpath(path)
        stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        for line_number, statement, terminator in collect_top_level_statements(stripped):
            if terminator != "{":
                continue
            if "operator" in statement:
                continue
            qualified = QUALIFIED_DEF_RE.search(statement)
            if qualified:
                full_name = qualified.group(1)
                owner_name = full_name.rsplit("::", 1)[0]
                owners = class_headers.get(owner_name)
                if owners and len(owners) == 1:
                    owner_header = next(iter(owners))
                    expected_cpp = paired_cpp_for_header(owner_header)
                    if cpp_rel != expected_cpp:
                        violations.append(
                            Violation(
                                kind="impl-mismatch",
                                relpath=cpp_rel,
                                line=line_number,
                                message=f"{full_name} is declared from {owner_header} but implemented in {cpp_rel}; expected {expected_cpp}.",
                            )
                        )
                continue

            match = FREE_DECL_RE.search(statement)
            if not match:
                continue
            name = match.group(1)
            if name in CONTROL_KEYWORDS:
                continue
            owners = free_headers.get(name)
            if owners and len(owners) == 1:
                owner_header = next(iter(owners))
                expected_cpp = paired_cpp_for_header(owner_header)
                if cpp_rel != expected_cpp:
                    violations.append(
                        Violation(
                            kind="impl-mismatch",
                            relpath=cpp_rel,
                            line=line_number,
                            message=f"{name} is declared from {owner_header} but implemented in {cpp_rel}; expected {expected_cpp}.",
                        )
                    )
    return violations


def collect_header_body_violations(headers: Iterable[Path]) -> list[Violation]:
    violations: list[Violation] = []
    for path in headers:
        header_rel = relpath(path)
        if header_rel in HEADER_BODY_ALLOWLIST:
            continue
        stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        for match in HEADER_BODY_RE.finditer(stripped):
            name = match.group(1)
            if name in CONTROL_KEYWORDS:
                continue
            prefix = stripped[max(0, match.start() - 80) : match.start()]
            if "template <" in prefix or "template<" in prefix:
                continue
            line_number = stripped.count("\n", 0, match.start()) + 1
            violations.append(
                Violation(
                    kind="header-body",
                    relpath=header_rel,
                    line=line_number,
                    message=f"Function body for {name} appears in a header; move non-template logic to a matching .cpp file.",
                )
            )
    return violations


def collect_cpp_header_violations(cpps: Iterable[Path]) -> list[Violation]:
    violations: list[Violation] = []
    for path in cpps:
        cpp_rel = relpath(path)
        if cpp_rel in CPP_WITHOUT_HEADER_ALLOWLIST:
            continue
        expected_header = PROJECT_ROOT / paired_header_for_cpp(cpp_rel)
        if expected_header.exists():
            continue
        violations.append(
            Violation(
                kind="missing-header",
                relpath=cpp_rel,
                line=1,
                message=f"{cpp_rel} has no matching header {paired_header_for_cpp(cpp_rel)}; add one or allowlist the translation unit.",
            )
        )
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="Check header/implementation ownership constraints.")
    parser.parse_args()

    files = list_project_files()
    headers = sorted(path for path in files if path.suffix == ".h")
    cpps = sorted(path for path in files if path.suffix == ".cpp")

    class_headers = collect_class_headers(headers)
    free_headers = collect_free_function_headers(headers)

    violations = []
    violations.extend(collect_cpp_header_violations(cpps))
    violations.extend(collect_cpp_definition_violations(cpps, class_headers, free_headers))
    violations.extend(collect_header_body_violations(headers))
    violations.sort(key=lambda item: (item.relpath, item.line, item.kind, item.message))

    if violations:
        for violation in violations:
            print(f"{violation.relpath}:{violation.line}: {violation.kind}: {violation.message}")
        print(f"\nArchitecture check failed with {len(violations)} violation(s).")
        return 1

    print(f"Architecture check passed for {len(headers)} headers and {len(cpps)} implementation files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
