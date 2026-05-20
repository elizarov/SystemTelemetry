#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import check_architecture
import check_includes
import check_source_policy
import source_dependency_graph


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = ("src", "tests", "resources")
SOURCE_SUFFIXES = {".cpp", ".h"}
LINT_SUFFIXES = SOURCE_SUFFIXES | {".rc"}


@dataclass(frozen=True)
class FileEntry:
    path: Path
    tracked: bool


@dataclass(frozen=True)
class IncludeDirective:
    line: int
    text: str
    quoted: bool


@dataclass(frozen=True)
class SourceRecord:
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
class SourceDependencyData:
    records: tuple[SourceRecord, ...]
    modules: dict[str, source_dependency_graph.Module]
    edges: dict[tuple[str, str], str]


def relpath(path: Path) -> str:
    return path.relative_to(PROJECT_ROOT).as_posix()


def has_root(relative: str, roots: tuple[str, ...]) -> bool:
    return any(relative.startswith(f"{root}/") for root in roots)


def is_excluded(relative: str) -> bool:
    excluded_prefixes = (
        *check_architecture.EXCLUDED_PREFIXES,
        *check_includes.EXCLUDED_PREFIXES,
        *check_source_policy.EXCLUDED_PREFIXES,
        *source_dependency_graph.EXCLUDED_PREFIXES,
    )
    return any(relative.startswith(prefix) for prefix in excluded_prefixes)


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


def is_lint_input(path: Path, tracked: bool) -> bool:
    relative = relpath(path)
    if path.suffix not in LINT_SUFFIXES:
        return False
    if not has_root(relative, SCAN_ROOTS):
        return False
    if is_excluded(relative):
        return False
    if path.suffix == ".rc" and not tracked:
        return False
    return True


def discover_lint_inputs() -> list[FileEntry]:
    tracked_lines = run_git_ls_files(list(SCAN_ROOTS))
    untracked_lines = run_git_ls_files(["--others", "--exclude-standard", *SCAN_ROOTS])

    if tracked_lines is None or untracked_lines is None:
        paths = {
            path.resolve()
            for root in SCAN_ROOTS
            for path in (PROJECT_ROOT / root).rglob("*")
            if path.is_file()
        }
        return [
            FileEntry(path=path, tracked=True)
            for path in sorted(paths, key=relpath)
            if is_lint_input(path, tracked=True)
        ]

    tracked_paths = project_paths_from_git(tracked_lines)
    untracked_paths = project_paths_from_git(untracked_lines)
    all_paths = sorted(tracked_paths | untracked_paths, key=relpath)
    return [
        FileEntry(path=path, tracked=path in tracked_paths)
        for path in all_paths
        if path.is_file() and is_lint_input(path, tracked=path in tracked_paths)
    ]


def scan_file(entry: FileEntry) -> SourceRecord:
    text = entry.path.read_text(encoding="utf-8")
    lines = tuple(text.splitlines())
    includes: list[IncludeDirective] = []
    nolint_lines: list[int] = []

    for line_number, line in enumerate(lines, start=1):
        if check_includes.NOLINT_RE.search(line):
            nolint_lines.append(line_number)

        match = source_dependency_graph.INCLUDE_RE.match(line)
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
    wide_literal_lines: tuple[int, ...] = ()
    if entry.path.suffix in SOURCE_SUFFIXES:
        stripped_text = check_source_policy.strip_comments_and_strings(text)
        stripped_lines = tuple(stripped_text.splitlines())
        wide_literal_lines = tuple(check_source_policy.find_undocumented_wide_literal_lines(text))

    return SourceRecord(
        path=entry.path,
        relative=relpath(entry.path),
        tracked=entry.tracked,
        text=text,
        lines=lines,
        includes=tuple(includes),
        nolint_lines=tuple(nolint_lines),
        stripped_text=stripped_text,
        stripped_lines=stripped_lines,
        wide_literal_lines=wide_literal_lines,
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


def scan_lint_inputs(entries: list[FileEntry], show_progress: bool) -> tuple[SourceRecord, ...]:
    records: list[SourceRecord] = []
    total = len(entries)
    use_single_line_progress = show_progress and sys.stdout.isatty()
    previous_progress_length = 0
    if show_progress:
        print(f"Scanning {total} lint input file(s)...")
    for index, entry in enumerate(entries, start=1):
        if show_progress:
            relative = relpath(entry.path)
            if use_single_line_progress:
                progress = truncate_progress_line(f"[{index}/{total}] lint-check ", relative)
                padding = " " * max(0, previous_progress_length - len(progress))
                sys.stdout.write(f"\r{progress}{padding}")
                sys.stdout.flush()
                previous_progress_length = len(progress)
            else:
                print(f"[{index}/{total}] lint-check {relative}", flush=True)
        records.append(scan_file(entry))
    if show_progress:
        print()
    return tuple(records)


def is_src_source_record(record: SourceRecord) -> bool:
    return (
        record.path.suffix in SOURCE_SUFFIXES
        and record.relative.startswith("src/")
        and not is_excluded(record.relative)
    )


def is_source_dependency_record(record: SourceRecord) -> bool:
    return is_src_source_record(record)


def is_architecture_record(record: SourceRecord) -> bool:
    return is_src_source_record(record)


def is_include_style_record(record: SourceRecord) -> bool:
    return (
        record.tracked
        and record.path.suffix in check_includes.CHECKED_SUFFIXES
        and has_root(record.relative, check_includes.SCANNED_ROOTS)
        and not is_excluded(record.relative)
    )


def is_source_policy_record(record: SourceRecord) -> bool:
    return (
        record.path.suffix in check_source_policy.CHECKED_SUFFIXES
        and has_root(record.relative, check_source_policy.SCANNED_ROOTS)
        and not is_excluded(record.relative)
    )


def collect_architecture_class_headers(records: list[SourceRecord]) -> dict[str, set[str]]:
    mapping: dict[str, set[str]] = {}
    for record in records:
        namespace_stack: list[str] = []
        for raw_line in record.stripped_lines:
            check_architecture.namespace_stack_for_line(raw_line, namespace_stack)
            for match in check_architecture.CLASS_DECL_RE.finditer(raw_line):
                name = match.group(1)
                suffix = raw_line[match.end() :]
                if "{" not in suffix:
                    continue
                mapping.setdefault(name, set()).add(record.relative)
                if namespace_stack:
                    mapping.setdefault("::".join([*namespace_stack, name]), set()).add(record.relative)
    return mapping


def collect_architecture_free_function_headers(records: list[SourceRecord]) -> dict[str, set[str]]:
    mapping: dict[str, set[str]] = {}
    for record in records:
        for _line_number, statement, terminator in check_architecture.collect_top_level_statements(record.stripped_text):
            if terminator != ";":
                continue
            if statement.startswith(("class ", "struct ", "enum ", "using ", "typedef ")):
                continue
            if "operator" in statement:
                continue
            match = check_architecture.FREE_DECL_RE.search(statement)
            if not match:
                continue
            name = match.group(1)
            if name in check_architecture.CONTROL_KEYWORDS:
                continue
            mapping.setdefault(name, set()).add(record.relative)
    return mapping


def collect_architecture_cpp_header_violations(
    records: list[SourceRecord],
) -> list[check_architecture.Violation]:
    violations: list[check_architecture.Violation] = []
    for record in records:
        if record.relative in check_architecture.CPP_WITHOUT_HEADER_ALLOWLIST:
            continue
        expected_header = PROJECT_ROOT / check_architecture.paired_header_for_cpp(record.relative)
        if expected_header.exists():
            continue
        violations.append(
            check_architecture.Violation(
                kind="missing-header",
                relpath=record.relative,
                line=1,
                message=(
                    f"{record.relative} has no matching header "
                    f"{check_architecture.paired_header_for_cpp(record.relative)}; "
                    "add one or allowlist the translation unit."
                ),
            )
        )
    return violations


def collect_architecture_cpp_definition_violations(
    records: list[SourceRecord],
    class_headers: dict[str, set[str]],
    free_headers: dict[str, set[str]],
) -> list[check_architecture.Violation]:
    violations: list[check_architecture.Violation] = []
    for record in records:
        for line_number, statement, terminator in check_architecture.collect_top_level_statements(record.stripped_text):
            if terminator != "{":
                continue
            if "operator" in statement:
                continue
            qualified = check_architecture.QUALIFIED_DEF_RE.search(statement)
            if qualified:
                full_name = qualified.group(1)
                owner_name = full_name.rsplit("::", 1)[0]
                owners = class_headers.get(owner_name)
                if owners and len(owners) == 1:
                    owner_header = next(iter(owners))
                    expected_cpp = check_architecture.paired_cpp_for_header(owner_header)
                    if record.relative != expected_cpp:
                        violations.append(
                            check_architecture.Violation(
                                kind="impl-mismatch",
                                relpath=record.relative,
                                line=line_number,
                                message=(
                                    f"{full_name} is declared from {owner_header} but implemented in "
                                    f"{record.relative}; expected {expected_cpp}."
                                ),
                            )
                        )
                continue

            match = check_architecture.FREE_DECL_RE.search(statement)
            if not match:
                continue
            name = match.group(1)
            if name in check_architecture.CONTROL_KEYWORDS:
                continue
            owners = free_headers.get(name)
            if owners and len(owners) == 1:
                owner_header = next(iter(owners))
                expected_cpp = check_architecture.paired_cpp_for_header(owner_header)
                if record.relative != expected_cpp:
                    violations.append(
                        check_architecture.Violation(
                            kind="impl-mismatch",
                            relpath=record.relative,
                            line=line_number,
                            message=(
                                f"{name} is declared from {owner_header} but implemented in "
                                f"{record.relative}; expected {expected_cpp}."
                            ),
                        )
                    )
    return violations


def collect_architecture_header_body_violations(
    records: list[SourceRecord],
) -> list[check_architecture.Violation]:
    violations: list[check_architecture.Violation] = []
    for record in records:
        if record.relative in check_architecture.HEADER_BODY_ALLOWLIST:
            continue
        for match in check_architecture.HEADER_BODY_RE.finditer(record.stripped_text):
            name = match.group(1)
            if name in check_architecture.CONTROL_KEYWORDS:
                continue
            prefix = record.stripped_text[max(0, match.start() - 80) : match.start()]
            if "template <" in prefix or "template<" in prefix:
                continue
            line_number = record.stripped_text.count("\n", 0, match.start()) + 1
            violations.append(
                check_architecture.Violation(
                    kind="header-body",
                    relpath=record.relative,
                    line=line_number,
                    message=f"Function body for {name} appears in a header; move non-template logic to a matching .cpp file.",
                )
            )
    return violations


def collect_architecture_violations(
    records: tuple[SourceRecord, ...],
) -> tuple[list[check_architecture.Violation], int, int]:
    architecture_records = [record for record in records if is_architecture_record(record)]
    headers = sorted((record for record in architecture_records if record.path.suffix == ".h"), key=lambda item: item.relative)
    cpps = sorted((record for record in architecture_records if record.path.suffix == ".cpp"), key=lambda item: item.relative)

    class_headers = collect_architecture_class_headers(headers)
    free_headers = collect_architecture_free_function_headers(headers)

    violations: list[check_architecture.Violation] = []
    violations.extend(collect_architecture_cpp_header_violations(cpps))
    violations.extend(collect_architecture_cpp_definition_violations(cpps, class_headers, free_headers))
    violations.extend(collect_architecture_header_body_violations(headers))
    violations.sort(key=lambda item: (item.relpath, item.line, item.kind, item.message))
    return violations, len(headers), len(cpps)


def collect_source_dependency_data(records: tuple[SourceRecord, ...]) -> SourceDependencyData:
    source_records = tuple(record for record in records if is_source_dependency_record(record))
    modules: dict[str, source_dependency_graph.Module] = {}
    module_by_file: dict[Path, str] = {}

    for record in source_records:
        module_name = source_dependency_graph.module_name_for(record.path, source_dependency_graph.SOURCE_ROOT)
        module = modules.setdefault(
            module_name,
            source_dependency_graph.Module(
                name=module_name,
                directory=source_dependency_graph.module_directory(module_name),
            ),
        )
        module.files.add(record.path)
        if record.path.suffix in source_dependency_graph.HEADER_SUFFIXES:
            module.header_files += 1
            module.header_loc += record.line_count
        elif record.path.suffix == ".cpp":
            module.cpp_files += 1
            module.cpp_loc += record.line_count
        module_by_file[record.path.resolve()] = module_name

    uses: list[source_dependency_graph.IncludeUse] = []
    for record in source_records:
        source_module = module_by_file[record.path.resolve()]
        kind = source_dependency_graph.classify_include_use(record.path)
        for include in record.includes:
            normalized = source_dependency_graph.normalize_include(include.text).lower()
            if normalized in source_dependency_graph.D2D_INCLUDE_NAMES:
                uses.append(
                    source_dependency_graph.IncludeUse(
                        source=source_module,
                        target=source_dependency_graph.EXTERNAL_D2D_MODULE,
                        kind=kind,
                    )
                )
                continue
            target_module = source_dependency_graph.resolve_include(
                record.path,
                include.text,
                source_dependency_graph.SOURCE_ROOT,
                module_by_file,
            )
            if target_module is None or target_module == source_module:
                continue
            uses.append(source_dependency_graph.IncludeUse(source=source_module, target=target_module, kind=kind))

    edges = source_dependency_graph.merge_edges(uses)
    if any(target == source_dependency_graph.EXTERNAL_D2D_MODULE for _, target in edges):
        modules.setdefault(
            source_dependency_graph.EXTERNAL_D2D_MODULE,
            source_dependency_graph.Module(
                name=source_dependency_graph.EXTERNAL_D2D_MODULE,
                directory="external",
            ),
        )

    return SourceDependencyData(records=source_records, modules=modules, edges=edges)


def print_large_source_files(records: tuple[SourceRecord, ...]) -> None:
    large_files = [
        source_dependency_graph.SourceFileLoc(path=record.path, loc=record.line_count)
        for record in records
        if record.line_count > source_dependency_graph.LARGE_SOURCE_FILE_LOC_THRESHOLD
    ]
    if not large_files:
        return

    print(f"Source files over {source_dependency_graph.format_loc_count(source_dependency_graph.LARGE_SOURCE_FILE_LOC_THRESHOLD)} LOC:")
    for source_file in sorted(large_files, key=lambda item: (-item.loc, relpath(item.path))):
        print(f"  {relpath(source_file.path)}: {source_dependency_graph.format_loc_count(source_file.loc)} LOC")


def write_source_dependency_outputs(
    data: SourceDependencyData,
    dot_output: Path,
    graphml_output: Path,
    svg_output: Path,
    skip_svg: bool,
) -> str | None:
    try:
        source_dependency_graph.write_dot(data.modules, data.edges, dot_output)
        source_dependency_graph.write_graphml(data.modules, data.edges, graphml_output)
        if not skip_svg:
            source_dependency_graph.write_svg(dot_output, svg_output)
    except (OSError, RuntimeError) as error:
        return str(error)
    return None


def collect_include_style_violations(records: tuple[SourceRecord, ...]) -> tuple[list[check_includes.Violation], int]:
    include_records = tuple(record for record in records if is_include_style_record(record))
    violations: list[check_includes.Violation] = []
    for record in include_records:
        for line_number in record.nolint_lines:
            violations.append(
                check_includes.Violation(
                    relpath=record.relative,
                    line=line_number,
                    message=(
                        "Local NOLINT suppressions are not allowed; add maintained false positives to the "
                        "lint tool allowlist instead."
                    ),
                )
            )
        for include in record.includes:
            if not include.quoted:
                continue
            resolved = check_includes.resolve_project_include(record.path, include.text)
            if resolved is None:
                continue
            target_path, root_name = resolved
            expected = check_includes.expected_include_for(target_path, root_name)
            normalized = check_includes.normalize_include(include.text)
            if normalized == expected:
                continue
            violations.append(
                check_includes.Violation(
                    relpath=record.relative,
                    line=include.line,
                    message=(
                        f'Project header "{include.text}" resolves to {check_includes.relpath(target_path)}; '
                        f'use "{expected}" from the {root_name} include root instead of a relative or local shorthand path.'
                    ),
                )
            )
    violations.sort(key=lambda item: (item.relpath, item.line, item.message))
    return violations, len(include_records)


def collect_source_policy_violations(
    records: tuple[SourceRecord, ...],
) -> tuple[list[check_source_policy.Violation], int]:
    policy_records = tuple(record for record in records if is_source_policy_record(record))
    violations: list[check_source_policy.Violation] = []
    for record in policy_records:
        for line_number, line in enumerate(record.stripped_lines, start=1):
            if check_source_policy.STD_FUNCTION_RE.search(line):
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            "std::function is not allowed in maintained source; use FunctionRef for synchronous "
                            "borrowed callbacks or a purpose-built interface when ownership must escape the call. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
            if check_source_policy.STD_FILESYSTEM_RE.search(line) or check_source_policy.FILESYSTEM_INCLUDE_RE.search(line):
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            "std::filesystem is not allowed in maintained source; use src/util/file_path.* helpers "
                            "so path handling stays Win32-backed without pulling filesystem machinery into the app. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
            if check_source_policy.STD_HASH_RE.search(line):
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            "std::hash is not allowed in maintained source; use a concrete project-owned hash helper "
                            "for fixed lookup tables so small caches do not grow broad STL hashing machinery. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
            if check_source_policy.STD_THREADING_RE.search(line) or check_source_policy.STD_THREADING_INCLUDE_RE.search(line):
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            "STL threading primitives are not allowed in maintained source; use LightweightMutex for "
                            "small locks and direct Win32 thread or event handles for worker wakeups so prior "
                            "size-optimization wins stay enforced. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
            if check_source_policy.CONDITIONAL_COMPILATION_RE.search(line):
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            "conditional compilation guards are not allowed in maintained source; keep code compiled "
                            "for every native target and let the linker remove unreferenced target-specific helpers. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
            for match in check_source_policy.WIN32_W_CALL_RE.finditer(line):
                function_name = match.group(1)
                if (record.relative, function_name) in check_source_policy.ALLOWED_W_API_CALLS_BY_FILE:
                    continue
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            f"{function_name} is not allowed in maintained source; CaseDash declares UTF-8 as "
                            "the process code page and calls Win32 A APIs by default. Keep W calls only for "
                            "documented wide-native APIs that have no A-style boundary. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
            if check_source_policy.OBSOLETE_UTF8_IDENTIFIER_RE.search(line):
                violations.append(
                    check_source_policy.Violation(
                        relpath=record.relative,
                        line=line_number,
                        message=(
                            "obsolete conversion-only Utf8 helper names are not allowed; UTF-8 is the app default, "
                            "so call A APIs directly or use no-suffix app helpers. "
                            f"See {check_source_policy.GUARDRAILS_DOC}."
                        ),
                    )
                )
        for line_number in record.wide_literal_lines:
            violations.append(
                check_source_policy.Violation(
                    relpath=record.relative,
                    line=line_number,
                    message=(
                        'wide literals must stay narrow UTF-8 by default; only const wchar_t string constants '
                        'initialized with L"..." and an end-of-line reason comment are allowed for wide-native '
                        "boundaries with no A-style API. "
                        f"See {check_source_policy.GUARDRAILS_DOC}."
                    ),
                )
            )
    violations.sort(key=lambda item: (item.relpath, item.line, item.message))
    return violations, len(policy_records)


def print_architecture_report(violations: list[check_architecture.Violation], header_count: int, cpp_count: int) -> None:
    print("Architecture check:")
    if violations:
        for violation in violations:
            print(f"{violation.relpath}:{violation.line}: {violation.kind}: {violation.message}")
        print(f"Architecture check failed with {len(violations)} violation(s).")
        return
    print(f"Architecture check passed for {header_count} headers and {cpp_count} implementation files.")


def print_source_dependency_report(
    data: SourceDependencyData,
    dot_output: Path,
    graphml_output: Path,
    svg_output: Path,
    skip_svg: bool,
    output_error: str | None,
    violations: list[source_dependency_graph.Violation],
    checked: bool,
) -> None:
    print("Source dependency check:")
    if output_error:
        print(f"Source dependency graph write failed: {output_error}")
    else:
        public_edges = sum(1 for kind in data.edges.values() if kind == "public")
        private_edges = sum(1 for kind in data.edges.values() if kind == "private")
        written_outputs = f"{dot_output} and {graphml_output}"
        if not skip_svg:
            written_outputs += f", and {svg_output}"
        print(
            f"Wrote {written_outputs} with {len(data.modules)} modules, {public_edges} public dependencies, "
            f"and {private_edges} private dependencies."
        )
    source_dependency_graph.print_package_loc_summary(data.modules)
    source_dependency_graph.print_package_dependency_components(data.modules, data.edges)
    print_large_source_files(data.records)
    if checked:
        if violations:
            source_dependency_graph.print_violations(violations)
            print(f"Source dependency check failed with {len(violations)} violation(s).")
            return
        print("Source dependency check passed.")


def print_include_style_report(violations: list[check_includes.Violation], file_count: int) -> None:
    print("Include style check:")
    if violations:
        for violation in violations:
            print(f"{violation.relpath}:{violation.line}: include-style: {violation.message}")
        print(f"Include style check failed with {len(violations)} violation(s).")
        return
    print(f"Include style check passed for {file_count} tracked source, test, and resource files.")


def print_source_policy_report(violations: list[check_source_policy.Violation], file_count: int) -> None:
    print("Source policy check:")
    if violations:
        for violation in violations:
            print(f"{violation.relpath}:{violation.line}: source-policy: {violation.message}")
        print(f"Source policy check failed with {len(violations)} violation(s).")
        return
    print(f"Source policy check passed for {file_count} tracked source and test files.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run combined architecture, source dependency, include style, and source policy lint checks."
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=source_dependency_graph.DEFAULT_DOT_PATH,
        help=f"DOT output path. Defaults to {source_dependency_graph.DEFAULT_DOT_PATH.relative_to(PROJECT_ROOT)}.",
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
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    graphml_output = args.graphml_output or args.output.with_suffix(".graphml")
    svg_output = args.svg_output or args.output.with_suffix(".svg")

    records = scan_lint_inputs(discover_lint_inputs(), show_progress=not args.no_progress)

    architecture_violations, header_count, cpp_count = collect_architecture_violations(records)
    source_dependency_data = collect_source_dependency_data(records)
    source_dependency_output_error = write_source_dependency_outputs(
        source_dependency_data,
        args.output,
        graphml_output,
        svg_output,
        args.skip_svg,
    )
    source_dependency_violations = (
        source_dependency_graph.check_graph_rules(source_dependency_data.edges) if args.check else []
    )
    include_style_violations, include_style_file_count = collect_include_style_violations(records)
    source_policy_violations, source_policy_file_count = collect_source_policy_violations(records)

    print_architecture_report(architecture_violations, header_count, cpp_count)
    print()
    print_source_dependency_report(
        source_dependency_data,
        args.output,
        graphml_output,
        svg_output,
        args.skip_svg,
        source_dependency_output_error,
        source_dependency_violations,
        args.check,
    )
    print()
    print_include_style_report(include_style_violations, include_style_file_count)
    print()
    print_source_policy_report(source_policy_violations, source_policy_file_count)

    if (
        architecture_violations
        or source_dependency_output_error
        or source_dependency_violations
        or include_style_violations
        or source_policy_violations
    ):
        print()
        print("Combined lint check failed.")
        return 1

    print()
    print("Combined lint check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
