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
GUARDRAILS_DOC = "docs/source_policy_guardrails.md"
STD_FUNCTION_RE = re.compile(r"\bstd\s*::\s*function\b")
STD_FILESYSTEM_RE = re.compile(r"\bstd\s*::\s*filesystem\b")
STD_HASH_RE = re.compile(r"\bstd\s*::\s*hash\b")
FILESYSTEM_INCLUDE_RE = re.compile(r"^\s*#\s*include\s*<\s*filesystem\s*>")
STD_THREADING_RE = re.compile(
    r"\bstd\s*::\s*(?:condition_variable(?:_any)?|jthread|mutex|recursive_mutex|shared_mutex|thread|timed_mutex)\b"
)
STD_THREADING_INCLUDE_RE = re.compile(r"^\s*#\s*include\s*<\s*(?:condition_variable|mutex|shared_mutex|thread)\s*>")
CONDITIONAL_COMPILATION_RE = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif|else|endif)\b")
CONST_WIDE_STRING_DECL_RE = re.compile(
    r"^\s*(?:(?:static|inline)\s+)*(?:constexpr|const)\b(?=[^=;\n]*\bwchar_t\b)[^=;\n]*=\s*$"
)
WIN32_W_CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*W)\s*\(")
ALLOWED_W_API_CALLS_BY_FILE = {
    ("src/util/command_line.cpp", "CommandLineToArgvW"),
    ("src/util/command_line.cpp", "GetCommandLineW"),
}
OBSOLETE_UTF8_IDENTIFIER_RE = re.compile(
    r"\b(?:"
    r"AddComboStringUtf8|AppendMenuUtf8|CleanProcessDisplayNameUtf8|LoadUtf8ResourceData|"
    r"ManagedStringFromUtf8|MessageBoxUtf8|ReadComboTextUtf8|ReadConfigFileUtf8|"
    r"ReadDialogControlTextUtf8|SetDiagnosticsUtf8|SetDialogControlTextUtf8|"
    r"SetWindowTextUtf8|Utf8FromAnsi|Utf8FromWide|WideFromUtf8|WriteConfigFileUtf8|"
    r"kAppTitleUtf8"
    r")\b"
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


def is_identifier_char(ch: str) -> bool:
    return ch == "_" or ch.isalnum()


def skip_quoted_literal(text: str, index: int, quote: str) -> int:
    index += 1
    while index < len(text):
        ch = text[index]
        if ch == "\\" and index + 1 < len(text):
            index += 2
            continue
        index += 1
        if ch == quote:
            break
    return index


def skip_raw_string_literal(text: str, index: int) -> int:
    delimiter_start = index + 2
    paren = text.find("(", delimiter_start)
    if paren < 0:
        return index + 2
    delimiter = text[delimiter_start:paren]
    terminator = ")" + delimiter + '"'
    end = text.find(terminator, paren + 1)
    return len(text) if end < 0 else end + len(terminator)


def line_bounds(text: str, index: int) -> tuple[int, int]:
    start = text.rfind("\n", 0, index) + 1
    end = text.find("\n", index)
    return start, len(text) if end < 0 else end


def is_allowed_const_wide_string_literal(text: str, prefix_index: int, literal_index: int, end_index: int) -> bool:
    if literal_index >= len(text) or text[literal_index] != '"':
        return False
    line_start, line_end = line_bounds(text, prefix_index)
    line = text[line_start:line_end]
    literal_end_in_line = end_index - line_start
    comment_index = line.find("//", literal_end_in_line)
    if comment_index < 0 or not line[comment_index + 2 :].strip():
        return False
    if line[literal_end_in_line:comment_index].strip() != ";":
        return False
    return CONST_WIDE_STRING_DECL_RE.match(line[: prefix_index - line_start]) is not None


def find_undocumented_wide_literal_lines(text: str) -> list[int]:
    lines: list[int] = []
    line = 1
    index = 0
    in_line_comment = False
    in_block_comment = False
    while index < len(text):
        ch = text[index]
        nxt = text[index + 1] if index + 1 < len(text) else ""
        if ch == "\n":
            line += 1
        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
            index += 1
            continue
        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                index += 2
            else:
                index += 1
            continue
        if ch == "/" and nxt == "/":
            in_line_comment = True
            index += 2
            continue
        if ch == "/" and nxt == "*":
            in_block_comment = True
            index += 2
            continue
        if ch == "L" and (index == 0 or not is_identifier_char(text[index - 1])):
            literal_index = index + 1
            if literal_index < len(text) and text[literal_index] == "R":
                literal_index += 1
            while literal_index < len(text) and text[literal_index] in " \t\r\n":
                literal_index += 1
            if literal_index < len(text) and text[literal_index] in "\"'":
                end = (
                    skip_raw_string_literal(text, literal_index - 1)
                    if text[literal_index - 1 : literal_index + 1] == 'R"'
                    else skip_quoted_literal(text, literal_index, text[literal_index])
                )
                if not is_allowed_const_wide_string_literal(text, index, literal_index, end):
                    lines.append(line)
                line += text[index:end].count("\n")
                index = end
                continue
        if ch == "R" and nxt == '"':
            end = skip_raw_string_literal(text, index)
            line += text[index:end].count("\n")
            index = end
            continue
        if ch in "\"'":
            end = skip_quoted_literal(text, index, ch)
            line += text[index:end].count("\n")
            index = end
            continue
        index += 1
    return lines


def collect_violations(files: list[Path]) -> list[Violation]:
    violations: list[Violation] = []
    for path in files:
        text = path.read_text(encoding="utf-8")
        stripped = strip_comments_and_strings(text)
        file_rel = relpath(path)
        for line_number, line in enumerate(stripped.splitlines(), start=1):
            if STD_FUNCTION_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "std::function is not allowed in maintained source; use FunctionRef for synchronous "
                            "borrowed callbacks or a purpose-built interface when ownership must escape the call. "
                            f"See {GUARDRAILS_DOC}."
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
                            "so path handling stays Win32-backed without pulling filesystem machinery into the app. "
                            f"See {GUARDRAILS_DOC}."
                        ),
                    )
                )
            if STD_HASH_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "std::hash is not allowed in maintained source; use a concrete project-owned hash helper "
                            "for fixed lookup tables so small caches do not grow broad STL hashing machinery. "
                            f"See {GUARDRAILS_DOC}."
                        ),
                    )
                )
            if STD_THREADING_RE.search(line) or STD_THREADING_INCLUDE_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "STL threading primitives are not allowed in maintained source; use LightweightMutex for "
                            "small locks and direct Win32 thread or event handles for worker wakeups so prior "
                            "size-optimization wins stay enforced. "
                            f"See {GUARDRAILS_DOC}."
                        ),
                    )
                )
            if CONDITIONAL_COMPILATION_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "conditional compilation guards are not allowed in maintained source; keep code compiled "
                            "for every native target and let the linker remove unreferenced target-specific helpers. "
                            f"See {GUARDRAILS_DOC}."
                        ),
                    )
                )
            for match in WIN32_W_CALL_RE.finditer(line):
                function_name = match.group(1)
                if (file_rel, function_name) in ALLOWED_W_API_CALLS_BY_FILE:
                    continue
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            f"{function_name} is not allowed in maintained source; CaseDash declares UTF-8 as "
                            "the process code page and calls Win32 A APIs by default. Keep W calls only for "
                            "documented wide-native APIs that have no A-style boundary. "
                            f"See {GUARDRAILS_DOC}."
                        ),
                    )
                )
            if OBSOLETE_UTF8_IDENTIFIER_RE.search(line):
                violations.append(
                    Violation(
                        relpath=file_rel,
                        line=line_number,
                        message=(
                            "obsolete conversion-only Utf8 helper names are not allowed; UTF-8 is the app default, "
                            "so call A APIs directly or use no-suffix app helpers. "
                            f"See {GUARDRAILS_DOC}."
                        ),
                    )
                )
        for line_number in find_undocumented_wide_literal_lines(text):
            violations.append(
                Violation(
                    relpath=file_rel,
                    line=line_number,
                    message=(
                        'wide literals must stay narrow UTF-8 by default; only const wchar_t string constants '
                        'initialized with L"..." and an end-of-line reason comment are allowed for wide-native '
                        "boundaries with no A-style API. "
                        f"See {GUARDRAILS_DOC}."
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
