#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
import re


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = PROJECT_ROOT / "src"
DEFAULT_DOT_PATH = PROJECT_ROOT / "build" / "architecture" / "source_dependencies.dot"
EXCLUDED_PREFIXES = ("src/vendor/",)
SOURCE_SUFFIXES = {".cpp", ".h"}
HEADER_SUFFIXES = {".h"}
INCLUDE_RE = re.compile(r'^\s*#include\s+(?:"([^"]+)"|<([^>]+)>)')


@dataclass
class Module:
    name: str
    directory: str
    files: set[Path] = field(default_factory=set)


@dataclass(frozen=True)
class IncludeUse:
    source: str
    target: str
    kind: str


def relpath(path: Path) -> str:
    return path.relative_to(PROJECT_ROOT).as_posix()


def is_project_source(path: Path) -> bool:
    relative = relpath(path)
    if path.suffix not in SOURCE_SUFFIXES:
        return False
    if not relative.startswith("src/"):
        return False
    return not any(relative.startswith(prefix) for prefix in EXCLUDED_PREFIXES)


def list_project_source_files(source_root: Path) -> list[Path]:
    try:
        result = subprocess.run(
            ["git", "ls-files", str(source_root.relative_to(PROJECT_ROOT))],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        files = [PROJECT_ROOT / line.strip() for line in result.stdout.splitlines() if line.strip()]
    except (OSError, subprocess.CalledProcessError, ValueError):
        files = list(source_root.rglob("*"))
    return sorted(path for path in files if path.is_file() and is_project_source(path))


def module_name_for(path: Path, source_root: Path) -> str:
    return path.relative_to(source_root).with_suffix("").as_posix()


def module_directory(module_name: str) -> str:
    directory = Path(module_name).parent.as_posix()
    return "." if directory == "." else directory


def collect_modules(files: list[Path], source_root: Path) -> tuple[dict[str, Module], dict[Path, str]]:
    modules: dict[str, Module] = {}
    module_by_file: dict[Path, str] = {}
    for path in files:
        module_name = module_name_for(path, source_root)
        module = modules.setdefault(module_name, Module(name=module_name, directory=module_directory(module_name)))
        module.files.add(path)
        module_by_file[path.resolve()] = module_name
    return modules, module_by_file


def normalize_include(include_text: str) -> str:
    return include_text.replace("\\", "/")


def resolve_include(current_file: Path, include_text: str, source_root: Path, module_by_file: dict[Path, str]) -> str | None:
    normalized = normalize_include(include_text)
    candidates = [
        current_file.parent / normalized,
        source_root / normalized,
        PROJECT_ROOT / normalized,
    ]
    for candidate in candidates:
        if not candidate.is_file():
            continue
        return module_by_file.get(candidate.resolve())
    return None


def classify_include_use(source_file: Path) -> str:
    return "public" if source_file.suffix in HEADER_SUFFIXES else "private"


def collect_include_uses(files: list[Path], source_root: Path, module_by_file: dict[Path, str]) -> list[IncludeUse]:
    uses: list[IncludeUse] = []
    for path in files:
        source_module = module_by_file[path.resolve()]
        kind = classify_include_use(path)
        for line in path.read_text(encoding="utf-8").splitlines():
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            include_text = match.group(1) or match.group(2)
            target_module = resolve_include(path, include_text, source_root, module_by_file)
            if target_module is None or target_module == source_module:
                continue
            uses.append(IncludeUse(source=source_module, target=target_module, kind=kind))
    return uses


def merge_edges(uses: list[IncludeUse]) -> dict[tuple[str, str], str]:
    edges: dict[tuple[str, str], str] = {}
    for use in uses:
        key = (use.source, use.target)
        previous = edges.get(key)
        if previous == "public" or use.kind == previous:
            continue
        edges[key] = "public" if use.kind == "public" else "private"
    return edges


def dot_quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def display_name(module_name: str) -> str:
    return module_name.replace("/", "\\")


def cluster_id(directory: str) -> str:
    if directory == ".":
        return "cluster_src"
    safe = "".join(ch if ch.isalnum() else "_" for ch in directory)
    return f"cluster_{safe or 'root'}"


def write_dot(modules: dict[str, Module], edges: dict[tuple[str, str], str], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    modules_by_directory: dict[str, list[Module]] = {}
    for module in modules.values():
        modules_by_directory.setdefault(module.directory, []).append(module)

    lines = [
        "digraph SourceDependencies {",
        "  graph [rankdir=LR, compound=true, fontname=\"Segoe UI\", labelloc=t, label=\"SystemTelemetry src module dependencies\"];",
        "  node [shape=box, style=\"rounded,filled\", fillcolor=\"#f8fafc\", color=\"#64748b\", fontname=\"Segoe UI\", fontsize=10];",
        "  edge [fontname=\"Segoe UI\", fontsize=9, arrowsize=0.7];",
        "",
    ]

    for directory in sorted(modules_by_directory):
        label = "src" if directory == "." else directory.replace("/", "\\")
        lines.extend(
            [
                f"  subgraph {cluster_id(directory)} {{",
                f"    label={dot_quote(label)};",
                "    color=\"#cbd5e1\";",
                "    style=\"rounded\";",
            ]
        )
        for module in sorted(modules_by_directory[directory], key=lambda item: item.name):
            lines.append(f"    {dot_quote(module.name)} [label={dot_quote(display_name(module.name))}];")
        lines.extend(["  }", ""])

    for (source, target), kind in sorted(edges.items()):
        if kind == "public":
            attributes = 'label="public", color="#2563eb", fontcolor="#2563eb", penwidth=1.6'
        else:
            attributes = 'label="private", color="#64748b", fontcolor="#64748b", style="dashed"'
        lines.append(f"  {dot_quote(source)} -> {dot_quote(target)} [{attributes}];")

    lines.append("}")
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Write a DOT graph of non-vendored src module dependencies.")
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=DEFAULT_DOT_PATH,
        help=f"DOT output path. Defaults to {DEFAULT_DOT_PATH.relative_to(PROJECT_ROOT)}.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    files = list_project_source_files(SOURCE_ROOT)
    modules, module_by_file = collect_modules(files, SOURCE_ROOT)
    uses = collect_include_uses(files, SOURCE_ROOT, module_by_file)
    edges = merge_edges(uses)
    write_dot(modules, edges, args.output)

    public_edges = sum(1 for kind in edges.values() if kind == "public")
    private_edges = sum(1 for kind in edges.values() if kind == "private")
    print(
        f"Wrote {args.output} with {len(modules)} modules, "
        f"{public_edges} public dependencies, and {private_edges} private dependencies."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
