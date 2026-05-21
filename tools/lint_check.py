#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from re import Pattern
from typing import Protocol

from lint import architecture, include_style, source_dependencies, source_policy
from lint.common import (
    CheckResult,
    CheckerContext,
    Config,
    FileRecord,
    IncludeDirective,
    config_strings,
    format_count,
    has_root,
    is_excluded,
    project_path,
    strip_comments_and_strings,
)


PROJECT_ROOT = Path.cwd().resolve()
DEFAULT_CONFIG_PATH = Path(__file__).with_name("lint_config.json")


class Checker(Protocol):
    def process_file(self, record: FileRecord) -> None:
        ...

    def finish(self, verbose: bool) -> CheckResult:
        ...


@dataclass(frozen=True)
class FileEntry:
    path: Path
    tracked: bool


@dataclass(frozen=True)
class ScanSettings:
    roots: tuple[str, ...]
    suffixes: frozenset[str]
    stripped_suffixes: frozenset[str]
    excluded_prefixes: tuple[str, ...]
    include_pattern: Pattern[str]


def relpath(path: Path) -> str:
    return path.relative_to(PROJECT_ROOT).as_posix()


def resolve_config_path(path: Path) -> Path:
    return path if path.is_absolute() else PROJECT_ROOT / path


def load_config(config_path: Path) -> Config:
    try:
        return json.loads(config_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ValueError(f"could not read {config_path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ValueError(f"{config_path}: {error.msg} at line {error.lineno}, column {error.colno}") from error


def parse_suffix_groups(config: Config) -> dict[str, frozenset[str]]:
    groups = config.get("suffix_groups", {})
    if not isinstance(groups, dict):
        raise ValueError("suffix_groups must be an object")
    return {str(name): frozenset(str(item) for item in values) for name, values in groups.items()}


def require_suffix_group(suffix_groups: dict[str, frozenset[str]], config_path: str, group_name: str) -> frozenset[str]:
    try:
        return suffix_groups[group_name]
    except KeyError as error:
        raise ValueError(f"{config_path} references unknown suffix group {group_name}") from error


def require_single_suffix_group(suffix_groups: dict[str, frozenset[str]], config_path: str, group_name: str) -> None:
    values = require_suffix_group(suffix_groups, config_path, group_name)
    if len(values) != 1:
        raise ValueError(f"{config_path} must reference a suffix group with exactly one suffix")


def validate_config(config: Config, suffix_groups: dict[str, frozenset[str]]) -> None:
    require_suffix_group(suffix_groups, "scan.suffix_group", str(config["scan"]["suffix_group"]))
    require_single_suffix_group(
        suffix_groups,
        "architecture.header_suffix_group",
        str(config["architecture"]["header_suffix_group"]),
    )
    require_single_suffix_group(
        suffix_groups,
        "architecture.implementation_suffix_group",
        str(config["architecture"]["implementation_suffix_group"]),
    )
    require_suffix_group(suffix_groups, "include_style.suffix_group", str(config["include_style"]["suffix_group"]))
    require_suffix_group(
        suffix_groups,
        "source_dependencies.suffix_group",
        str(config["source_dependencies"]["suffix_group"]),
    )
    require_suffix_group(
        suffix_groups,
        "source_dependencies.header_suffix_group",
        str(config["source_dependencies"]["header_suffix_group"]),
    )
    require_suffix_group(suffix_groups, "source_policy.suffix_group", str(config["source_policy"]["suffix_group"]))
    source_dependency_roots = config_strings(config["source_dependencies"], "roots")
    if len(source_dependency_roots) != 1:
        raise ValueError("source_dependencies.roots must contain exactly one root")


def parse_scan_settings(config: Config, suffix_groups: dict[str, frozenset[str]]) -> ScanSettings:
    scan_config = config["scan"]
    return ScanSettings(
        roots=config_strings(scan_config, "roots"),
        suffixes=require_suffix_group(suffix_groups, "scan.suffix_group", str(scan_config["suffix_group"])),
        stripped_suffixes=require_suffix_group(suffix_groups, "suffix_groups.source", "source"),
        excluded_prefixes=config_strings(scan_config, "excluded_prefixes"),
        include_pattern=re.compile(str(scan_config["include_pattern"])),
    )


def run_git_ls_files(args: list[str]) -> list[str] | None:
    try:
        result = subprocess.run(
            ["git", "ls-files", *args],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def project_paths_from_git(lines: list[str]) -> set[Path]:
    return {(PROJECT_ROOT / line).resolve() for line in lines}


def is_lint_input(path: Path, tracked: bool, settings: ScanSettings) -> bool:
    relative = relpath(path)
    if path.suffix not in settings.suffixes:
        return False
    if settings.roots and not has_root(relative, settings.roots):
        return False
    if is_excluded(relative, settings.excluded_prefixes):
        return False
    return True


def discover_lint_inputs(settings: ScanSettings) -> list[FileEntry]:
    tracked_lines = run_git_ls_files(list(settings.roots))
    untracked_lines = run_git_ls_files(["--others", "--exclude-standard", *settings.roots])

    if tracked_lines is None or untracked_lines is None:
        paths = {
            path.resolve()
            for root in settings.roots
            for path in (PROJECT_ROOT / root).rglob("*")
            if path.is_file()
        }
        return [
            FileEntry(path=path, tracked=True)
            for path in sorted(paths, key=relpath)
            if is_lint_input(path, tracked=True, settings=settings)
        ]

    tracked_paths = project_paths_from_git(tracked_lines)
    untracked_paths = project_paths_from_git(untracked_lines)
    all_paths = sorted(tracked_paths | untracked_paths, key=relpath)
    return [
        FileEntry(path=path, tracked=path in tracked_paths)
        for path in all_paths
        if path.is_file() and is_lint_input(path, tracked=path in tracked_paths, settings=settings)
    ]


def scan_file(entry: FileEntry, settings: ScanSettings) -> FileRecord:
    text = entry.path.read_text(encoding="utf-8")
    lines = tuple(text.splitlines())
    includes: list[IncludeDirective] = []

    for line_number, line in enumerate(lines, start=1):
        match = settings.include_pattern.match(line)
        if match:
            quoted_text = match.group(1)
            includes.append(
                IncludeDirective(
                    line=line_number,
                    text=quoted_text or match.group(2),
                    quoted=quoted_text is not None,
                )
            )

    stripped_text = ""
    stripped_lines: tuple[str, ...] = ()
    if entry.path.suffix in settings.stripped_suffixes:
        stripped_text = strip_comments_and_strings(text)
        stripped_lines = tuple(stripped_text.splitlines())

    return FileRecord(
        path=entry.path,
        relative=relpath(entry.path),
        tracked=entry.tracked,
        text=text,
        lines=lines,
        includes=tuple(includes),
        stripped_text=stripped_text,
        stripped_lines=stripped_lines,
    )


def truncate_progress_line(prefix: str, relative: str) -> str:
    columns = shutil.get_terminal_size((120, 20)).columns
    if columns <= 1:
        return f"{prefix}{relative}"
    max_length = columns - 1
    full_line = f"{prefix}{relative}"
    if len(full_line) <= max_length:
        return full_line
    path_budget = max_length - len(prefix)
    if path_budget <= 3:
        return full_line[:max_length]
    return f"{prefix}...{relative[-(path_budget - 3):]}"


def scan_lint_inputs(entries: list[FileEntry], settings: ScanSettings, checkers: tuple[Checker, ...], show_progress: bool) -> tuple[FileRecord, ...]:
    records: list[FileRecord] = []
    total = len(entries)
    use_single_line_progress = show_progress and sys.stdout.isatty()
    previous_progress_length = 0
    for index, entry in enumerate(entries, start=1):
        if use_single_line_progress:
            relative = relpath(entry.path)
            progress = truncate_progress_line(f"[{index}/{total}] lint-check ", relative)
            padding = " " * max(0, previous_progress_length - len(progress))
            sys.stdout.write(f"\r{progress}{padding}")
            sys.stdout.flush()
            previous_progress_length = len(progress)
        record = scan_file(entry, settings)
        for checker in checkers:
            checker.process_file(record)
        records.append(record)
    if use_single_line_progress:
        sys.stdout.write(f"\r{' ' * previous_progress_length}\r")
        sys.stdout.flush()
    return tuple(records)


def absolute_project_path(path: Path) -> Path:
    return path if path.is_absolute() else PROJECT_ROOT / path


def parse_config_args() -> Path:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG_PATH,
    )
    args, _unknown = parser.parse_known_args()
    return resolve_config_path(args.config)


def parse_args(config: Config, config_path: Path) -> argparse.Namespace:
    source_dependency_config = config["source_dependencies"]
    default_dot_path = project_path(PROJECT_ROOT, str(source_dependency_config["default_dot_output"]))
    parser = argparse.ArgumentParser(
        description="Run combined architecture, source dependency, include style, and source policy lint checks."
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=config_path,
        help=f"Lint configuration file. Defaults to {DEFAULT_CONFIG_PATH}. Scan roots are relative to the current working directory.",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=default_dot_path,
        help=f"DOT output path. Defaults to {default_dot_path.relative_to(PROJECT_ROOT)}.",
    )
    parser.add_argument(
        "--graphml-output",
        type=Path,
        default=None,
        help="GraphML output path. Defaults to the DOT output path with a .graphml suffix.",
    )
    parser.add_argument(
        "--svg-output",
        type=Path,
        default=None,
        help="SVG output path. Defaults to the DOT output path with a .svg suffix.",
    )
    parser.add_argument(
        "--skip-svg",
        action="store_true",
        help="Skip Graphviz SVG rendering and write only DOT and GraphML outputs.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail when source dependency architecture rules are violated.",
    )
    parser.add_argument(
        "--no-progress",
        action="store_true",
        help="Do not print per-file scan progress.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print source files over the configured LOC threshold and topological dependency details.",
    )
    return parser.parse_args()


def create_context(args: argparse.Namespace, settings: ScanSettings, suffix_groups: dict[str, frozenset[str]]) -> CheckerContext:
    dot_output = absolute_project_path(args.output)
    graphml_output = absolute_project_path(args.graphml_output) if args.graphml_output else dot_output.with_suffix(".graphml")
    svg_output = absolute_project_path(args.svg_output) if args.svg_output else dot_output.with_suffix(".svg")
    return CheckerContext(
        project_root=PROJECT_ROOT,
        suffix_groups=suffix_groups,
        excluded_prefixes=settings.excluded_prefixes,
        check_dependencies=bool(args.check),
        dot_output=dot_output,
        graphml_output=graphml_output,
        svg_output=svg_output,
        skip_svg=bool(args.skip_svg),
    )


def create_checkers(config: Config, context: CheckerContext) -> tuple[Checker, ...]:
    return (
        architecture.create_checker(config["architecture"], context),
        source_dependencies.create_checker(config["source_dependencies"], context),
        include_style.create_checker(config["include_style"], context),
        source_policy.create_checker(config["source_policy"], context),
    )


def print_scanned_loc_summary(records: tuple[FileRecord, ...]) -> None:
    total_loc = sum(record.line_count for record in records)
    print(f"Scanned {format_count(total_loc)} LOC across {len(records)} lint input file(s).")


def print_failure_result(result: CheckResult) -> None:
    print(result.title)
    for error in result.errors:
        print(error)
    for finding in result.findings:
        print(f"{finding.location}: {finding.kind}: {finding.message}")
    if result.summary:
        print(result.summary)


def format_elapsed(seconds: float) -> str:
    if seconds < 1:
        return f"{round(seconds * 1000):.0f}ms"
    return f"{seconds:.3f}s"


def main() -> int:
    started = time.perf_counter()
    try:
        config_path = parse_config_args()
        config = load_config(config_path)
        suffix_groups = parse_suffix_groups(config)
        validate_config(config, suffix_groups)
        settings = parse_scan_settings(config, suffix_groups)
    except ValueError as error:
        print(f"lint config error: {error}", file=sys.stderr)
        print(f"Lint failed in {format_elapsed(time.perf_counter() - started)}.")
        return 2
    args = parse_args(config, config_path)
    context = create_context(args, settings, suffix_groups)
    checkers = create_checkers(config, context)

    records = scan_lint_inputs(
        discover_lint_inputs(settings),
        settings,
        checkers,
        show_progress=not args.no_progress,
    )
    results = tuple(checker.finish(args.verbose) for checker in checkers)

    printed_report = False
    for result in results:
        if not result.failed:
            continue
        if printed_report:
            print()
        print_failure_result(result)
        printed_report = True

    if any(result.failed for result in results):
        if printed_report:
            print()
        print(f"Lint failed in {format_elapsed(time.perf_counter() - started)}.")
        return 1

    print_scanned_loc_summary(records)
    if args.verbose:
        verbose_lines = [line for result in results for line in result.verbose_lines]
        if verbose_lines:
            print()
            for line in verbose_lines:
                print(line)
    print(f"Lint succeeded in {format_elapsed(time.perf_counter() - started)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
