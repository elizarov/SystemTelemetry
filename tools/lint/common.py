from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any


Config = dict[str, Any]


@dataclass(frozen=True)
class IncludeDirective:
    line: int
    text: str
    quoted: bool


@dataclass(frozen=True)
class FileRecord:
    path: Path
    relative: str
    tracked: bool
    text: str
    lines: tuple[str, ...]
    includes: tuple[IncludeDirective, ...]
    stripped_text: str
    stripped_lines: tuple[str, ...]

    @property
    def line_count(self) -> int:
        return len(self.lines)


@dataclass(frozen=True)
class CheckerContext:
    project_root: Path
    suffix_groups: dict[str, frozenset[str]]
    excluded_prefixes: tuple[str, ...]
    check_dependencies: bool
    dot_output: Path
    graphml_output: Path
    svg_output: Path
    skip_svg: bool


@dataclass(frozen=True)
class Finding:
    location: str
    kind: str
    message: str


@dataclass(frozen=True)
class CheckResult:
    title: str
    findings: tuple[Finding, ...] = ()
    errors: tuple[str, ...] = ()
    summary: str = ""
    verbose_lines: tuple[str, ...] = ()

    @property
    def failed(self) -> bool:
        return bool(self.findings or self.errors)


def config_strings(config: Config, key: str, default: tuple[str, ...] = ()) -> tuple[str, ...]:
    value = config.get(key)
    if value is None:
        return default
    return tuple(str(item) for item in value)


def suffix_group(context: CheckerContext, group_name: str) -> frozenset[str]:
    try:
        return context.suffix_groups[group_name]
    except KeyError as error:
        raise ValueError(f"unknown suffix group {group_name}") from error


def has_root(relative: str, roots: tuple[str, ...]) -> bool:
    return any(relative == root or relative.startswith(f"{root}/") for root in roots)


def is_excluded(relative: str, prefixes: tuple[str, ...]) -> bool:
    return any(relative.startswith(prefix) for prefix in prefixes)


def normalize_include(include_text: str) -> str:
    return include_text.replace("\\", "/")


def format_count(value: int) -> str:
    return f"{value:,}"


def project_path(project_root: Path, value: str | Path) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return project_root / path


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
