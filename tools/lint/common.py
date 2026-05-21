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
    nolint_lines: tuple[int, ...]
    stripped_text: str
    stripped_lines: tuple[str, ...]
    wide_literal_lines: tuple[int, ...]

    @property
    def line_count(self) -> int:
        return len(self.lines)


@dataclass(frozen=True)
class CheckerContext:
    project_root: Path
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
