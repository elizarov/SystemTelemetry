#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET
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
GRAPHML_NS = "http://graphml.graphdrawing.org/xmlns"
EXTERNAL_D2D_MODULE = "d2d"
D2D_INCLUDE_NAMES = {
    "d2d1.h",
    "dwrite.h",
    "wincodec.h",
    "wrl/client.h",
}
LARGE_SOURCE_FILE_LOC_THRESHOLD = 1_000


@dataclass
class Module:
    name: str
    directory: str
    files: set[Path] = field(default_factory=set)
    header_files: int = 0
    header_loc: int = 0
    cpp_files: int = 0
    cpp_loc: int = 0

    @property
    def total_loc(self) -> int:
        return self.header_loc + self.cpp_loc


@dataclass
class PackageLocSummary:
    header_files: int = 0
    header_loc: int = 0
    cpp_files: int = 0
    cpp_loc: int = 0

    @property
    def total_loc(self) -> int:
        return self.header_loc + self.cpp_loc


@dataclass(frozen=True)
class SourceFileLoc:
    path: Path
    loc: int


@dataclass(frozen=True)
class PackageDependencyComponent:
    packages: tuple[str, ...]
    total_loc: int


@dataclass(frozen=True)
class IncludeUse:
    source: str
    target: str
    kind: str


@dataclass(frozen=True)
class Violation:
    kind: str
    source: str
    target: str
    message: str


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
        tracked = subprocess.run(
            ["git", "ls-files", str(source_root.relative_to(PROJECT_ROOT))],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        untracked = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard", str(source_root.relative_to(PROJECT_ROOT))],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        paths = tracked.stdout.splitlines() + untracked.stdout.splitlines()
        files = [PROJECT_ROOT / line.strip() for line in paths if line.strip()]
    except (OSError, subprocess.CalledProcessError, ValueError):
        files = list(source_root.rglob("*"))
    return sorted(path for path in files if path.is_file() and is_project_source(path))


def module_name_for(path: Path, source_root: Path) -> str:
    return path.relative_to(source_root).with_suffix("").as_posix()


def module_directory(module_name: str) -> str:
    directory = Path(module_name).parent.as_posix()
    return "." if directory == "." else directory


def count_source_lines(path: Path) -> int:
    return len(path.read_text(encoding="utf-8").splitlines())


def collect_modules(files: list[Path], source_root: Path) -> tuple[dict[str, Module], dict[Path, str]]:
    modules: dict[str, Module] = {}
    module_by_file: dict[Path, str] = {}
    for path in files:
        module_name = module_name_for(path, source_root)
        module = modules.setdefault(module_name, Module(name=module_name, directory=module_directory(module_name)))
        module.files.add(path)
        line_count = count_source_lines(path)
        if path.suffix in HEADER_SUFFIXES:
            module.header_files += 1
            module.header_loc += line_count
        elif path.suffix == ".cpp":
            module.cpp_files += 1
            module.cpp_loc += line_count
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
            normalized = normalize_include(include_text).lower()
            if normalized in D2D_INCLUDE_NAMES:
                uses.append(IncludeUse(source=source_module, target=EXTERNAL_D2D_MODULE, kind=kind))
                continue
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


def top_level_package(module_name: str) -> str:
    return module_name.split("/", 1)[0]


def is_package_private_module(module_name: str) -> bool:
    return len(module_name.split("/")) > 2


def check_package_encapsulation(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    for source, target in sorted(edges):
        if not is_package_private_module(target):
            continue
        source_package = top_level_package(source)
        target_package = top_level_package(target)
        if source_package == target_package:
            continue
        violations.append(
            Violation(
                kind="package-encapsulation",
                source=source,
                target=target,
                message=(
                    f"{source} depends on package-private module {target}; "
                    f"only src/{target_package}/ files may include modules below src/{target_package}/*/."
                ),
            )
        )
    return violations


def check_util_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    for source, target in sorted(edges):
        if top_level_package(source) != "util":
            continue
        if top_level_package(target) == "util":
            continue
        violations.append(
            Violation(
                kind="layer-util",
                source=source,
                target=target,
                message=f"{source} is in the util layer and must not depend on project module {target}.",
            )
        )
    return violations


def check_config_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    allowed_targets = {"config", "util"}
    for source, target in sorted(edges):
        if top_level_package(source) != "config":
            continue
        if top_level_package(target) in allowed_targets:
            continue
        violations.append(
            Violation(
                kind="layer-config",
                source=source,
                target=target,
                message=(
                    f"{source} is in the config layer and must depend only on config or util modules, "
                    f"not project module {target}."
                ),
            )
        )
    return violations


def check_telemetry_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    allowed_targets = {"telemetry", "config", "util"}
    for source, target in sorted(edges):
        if top_level_package(source) != "telemetry":
            continue
        if top_level_package(target) in allowed_targets:
            continue
        violations.append(
            Violation(
                kind="layer-telemetry",
                source=source,
                target=target,
                message=(
                    f"{source} is in the telemetry layer and must depend only on telemetry, config, or util modules, "
                    f"not project module {target}."
                ),
            )
        )
    return violations


def check_widget_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    allowed_targets = {"widget", "renderer", "telemetry", "config", "util"}
    for source, target in sorted(edges):
        if top_level_package(source) != "widget":
            continue
        if top_level_package(target) in allowed_targets:
            continue
        violations.append(
            Violation(
                kind="layer-widget",
                source=source,
                target=target,
                message=(
                    f"{source} is in the widget layer and must depend only on widget, telemetry, config, or util "
                    f"modules, not project module {target}."
                ),
            )
        )
    return violations


def check_layout_model_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    allowed_targets = {"layout_model", "widget", "renderer", "telemetry", "config", "util"}
    for source, target in sorted(edges):
        if top_level_package(source) != "layout_model":
            continue
        if top_level_package(target) in allowed_targets:
            continue
        violations.append(
            Violation(
                kind="layer-layout-model",
                source=source,
                target=target,
                message=(
                    f"{source} is in the layout_model layer and must depend only on layout_model, widget, renderer, "
                    f"telemetry, config, or util modules, not project module {target}."
                ),
            )
        )
    return violations


def check_renderer_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    allowed_targets = {"renderer", "config", "util"}
    for source, target in sorted(edges):
        if top_level_package(source) != "renderer":
            continue
        if target == EXTERNAL_D2D_MODULE:
            continue
        if top_level_package(target) in allowed_targets:
            continue
        violations.append(
            Violation(
                kind="layer-renderer",
                source=source,
                target=target,
                message=(
                    f"{source} is in the renderer layer and must depend only on renderer, config, or util modules, "
                    f"not project module {target}."
                ),
            )
        )
    return violations


def check_d2d_layer(edges: dict[tuple[str, str], str]) -> list[Violation]:
    violations: list[Violation] = []
    for source, target in sorted(edges):
        if target != EXTERNAL_D2D_MODULE:
            continue
        if top_level_package(source) == "renderer":
            continue
        violations.append(
            Violation(
                kind="layer-d2d",
                source=source,
                target=target,
                message=f"{source} must not depend on Direct2D/DirectWrite/WIC/WRL module {target}; only renderer may.",
            )
        )
    return violations


def check_graph_rules(edges: dict[tuple[str, str], str]) -> list[Violation]:
    return [
        *check_package_encapsulation(edges),
        *check_util_layer(edges),
        *check_config_layer(edges),
        *check_telemetry_layer(edges),
        *check_widget_layer(edges),
        *check_layout_model_layer(edges),
        *check_renderer_layer(edges),
        *check_d2d_layer(edges),
    ]


def print_violations(violations: list[Violation]) -> None:
    for violation in violations:
        print(f"{violation.source} -> {violation.target}: {violation.kind}: {violation.message}")


def summarize_package_loc(modules: dict[str, Module]) -> dict[str, PackageLocSummary]:
    summaries: dict[str, PackageLocSummary] = {}
    for module in modules.values():
        package = top_level_package(module.name)
        if package == EXTERNAL_D2D_MODULE:
            continue
        summary = summaries.setdefault(package, PackageLocSummary())
        summary.header_files += module.header_files
        summary.header_loc += module.header_loc
        summary.cpp_files += module.cpp_files
        summary.cpp_loc += module.cpp_loc
    return summaries


def collect_package_dependencies(modules: dict[str, Module], edges: dict[tuple[str, str], str]) -> dict[str, set[str]]:
    dependencies: dict[str, set[str]] = {
        top_level_package(module.name): set()
        for module in modules.values()
        if top_level_package(module.name) != EXTERNAL_D2D_MODULE
    }
    for source, target in edges:
        source_package = top_level_package(source)
        target_package = top_level_package(target)
        if source_package == EXTERNAL_D2D_MODULE or target_package == EXTERNAL_D2D_MODULE:
            continue
        dependencies.setdefault(source_package, set())
        dependencies.setdefault(target_package, set())
        if source_package != target_package:
            dependencies[source_package].add(target_package)
    return dependencies


def find_strongly_connected_components(graph: dict[str, set[str]]) -> list[tuple[str, ...]]:
    index = 0
    stack: list[str] = []
    on_stack: set[str] = set()
    indexes: dict[str, int] = {}
    lowlinks: dict[str, int] = {}
    components: list[tuple[str, ...]] = []

    def connect(node: str) -> None:
        nonlocal index
        indexes[node] = index
        lowlinks[node] = index
        index += 1
        stack.append(node)
        on_stack.add(node)

        for target in sorted(graph[node]):
            if target not in indexes:
                connect(target)
                lowlinks[node] = min(lowlinks[node], lowlinks[target])
            elif target in on_stack:
                lowlinks[node] = min(lowlinks[node], indexes[target])

        if lowlinks[node] != indexes[node]:
            return

        component: list[str] = []
        while True:
            member = stack.pop()
            on_stack.remove(member)
            component.append(member)
            if member == node:
                break
        components.append(tuple(sorted(component)))

    for node in sorted(graph):
        if node not in indexes:
            connect(node)
    return components


def topologically_sort_components(
    graph: dict[str, set[str]], components: list[tuple[str, ...]]
) -> list[tuple[str, ...]]:
    component_by_package = {
        package: component_index for component_index, component in enumerate(components) for package in component
    }
    component_edges: dict[int, set[int]] = {index: set() for index in range(len(components))}
    indegrees: dict[int, int] = {index: 0 for index in range(len(components))}

    for source, targets in graph.items():
        source_component = component_by_package[source]
        for target in targets:
            target_component = component_by_package[target]
            if source_component == target_component or target_component in component_edges[source_component]:
                continue
            component_edges[source_component].add(target_component)
            indegrees[target_component] += 1

    ready = sorted((index for index, indegree in indegrees.items() if indegree == 0), key=lambda item: components[item])
    ordered: list[tuple[str, ...]] = []
    while ready:
        component_index = ready.pop(0)
        ordered.append(components[component_index])
        for target_component in sorted(component_edges[component_index], key=lambda item: components[item]):
            indegrees[target_component] -= 1
            if indegrees[target_component] == 0:
                ready.append(target_component)
        ready.sort(key=lambda item: components[item])
    return ordered


def summarize_package_dependency_components(
    modules: dict[str, Module], edges: dict[tuple[str, str], str]
) -> list[PackageDependencyComponent]:
    summaries = summarize_package_loc(modules)
    graph = collect_package_dependencies(modules, edges)
    components = topologically_sort_components(graph, find_strongly_connected_components(graph))
    return [
        PackageDependencyComponent(
            packages=component,
            total_loc=sum(summaries[package].total_loc for package in component),
        )
        for component in components
    ]


def format_loc_count(value: int) -> str:
    return f"{value:,}"


def format_module_loc_annotation(module: Module) -> str:
    return (
        f".h {format_loc_count(module.header_loc)} LOC | "
        f".cpp {format_loc_count(module.cpp_loc)} LOC | "
        f"total {format_loc_count(module.total_loc)} LOC"
    )


def format_node_label(module: Module) -> str:
    return f"{display_name(module.name)}\n{format_module_loc_annotation(module)}"


def print_package_loc_summary(modules: dict[str, Module]) -> None:
    print("LOC totals by top-level package:")
    total = PackageLocSummary()
    for package, summary in sorted(summarize_package_loc(modules).items()):
        total.header_files += summary.header_files
        total.header_loc += summary.header_loc
        total.cpp_files += summary.cpp_files
        total.cpp_loc += summary.cpp_loc
        print(
            f"  {package}: {format_loc_count(summary.total_loc)} total "
            f"(.h {format_loc_count(summary.header_loc)} in {summary.header_files} file(s), "
            f".cpp {format_loc_count(summary.cpp_loc)} in {summary.cpp_files} file(s))"
        )
    print(
        f"  total: {format_loc_count(total.total_loc)} total "
        f"(.h {format_loc_count(total.header_loc)} in {total.header_files} file(s), "
        f".cpp {format_loc_count(total.cpp_loc)} in {total.cpp_files} file(s))"
    )


def print_package_dependency_components(modules: dict[str, Module], edges: dict[tuple[str, str], str]) -> None:
    print("Package dependency components in topological order:")
    for index, component in enumerate(summarize_package_dependency_components(modules, edges), start=1):
        print(
            f"  {index}. {format_loc_count(component.total_loc)} LOC: "
            f"{', '.join(component.packages)}"
        )


def print_large_source_files(files: list[Path]) -> None:
    large_files: list[SourceFileLoc] = []
    for path in files:
        line_count = count_source_lines(path)
        if line_count > LARGE_SOURCE_FILE_LOC_THRESHOLD:
            large_files.append(SourceFileLoc(path=path, loc=line_count))
    if not large_files:
        return

    print(f"Source files over {format_loc_count(LARGE_SOURCE_FILE_LOC_THRESHOLD)} LOC:")
    for source_file in sorted(large_files, key=lambda item: (-item.loc, relpath(item.path))):
        print(f"  {relpath(source_file.path)}: {format_loc_count(source_file.loc)} LOC")


def dot_quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


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
        "  graph [rankdir=LR, compound=true, newrank=true, fontname=\"Segoe UI\", labelloc=t, label=\"SystemTelemetry src module dependencies\"];",
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
            lines.append(f"    {dot_quote(module.name)} [label={dot_quote(format_node_label(module))}];")
        lines.extend(["  }", ""])

    for (source, target), kind in sorted(edges.items()):
        if kind == "public":
            attributes = 'label="public", color="#2563eb", fontcolor="#2563eb", penwidth=1.6'
        else:
            attributes = 'label="private", color="#64748b", fontcolor="#64748b", style="dashed"'
        lines.append(f"  {dot_quote(source)} -> {dot_quote(target)} [{attributes}];")

    lines.append("}")
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_svg(dot_path: Path, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(
            ["dot", "-Tsvg", str(dot_path), "-o", str(output_path)],
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as error:
        raise RuntimeError("Graphviz dot was not found on PATH; install Graphviz to render the SVG graph.") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout).strip()
        message = f"Graphviz dot failed while rendering {output_path}."
        if detail:
            message += f" {detail}"
        raise RuntimeError(message) from error


def graphml_tag(name: str) -> str:
    return f"{{{GRAPHML_NS}}}{name}"


def add_graphml_data(parent: ET.Element, key: str, value: str) -> None:
    data = ET.SubElement(parent, graphml_tag("data"), key=key)
    data.text = value


def write_graphml(modules: dict[str, Module], edges: dict[tuple[str, str], str], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    ET.register_namespace("", GRAPHML_NS)
    root = ET.Element(graphml_tag("graphml"))
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="node_label",
        **{"for": "node", "attr.name": "label", "attr.type": "string"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="node_directory",
        **{"for": "node", "attr.name": "directory", "attr.type": "string"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="node_header_loc",
        **{"for": "node", "attr.name": "header_loc", "attr.type": "int"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="node_cpp_loc",
        **{"for": "node", "attr.name": "cpp_loc", "attr.type": "int"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="node_total_loc",
        **{"for": "node", "attr.name": "total_loc", "attr.type": "int"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="node_loc_annotation",
        **{"for": "node", "attr.name": "loc_annotation", "attr.type": "string"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="edge_label",
        **{"for": "edge", "attr.name": "label", "attr.type": "string"},
    )
    ET.SubElement(
        root,
        graphml_tag("key"),
        id="edge_kind",
        **{"for": "edge", "attr.name": "kind", "attr.type": "string"},
    )

    graph = ET.SubElement(root, graphml_tag("graph"), id="SourceDependencies", edgedefault="directed")
    module_ids = {module_name: f"n{index}" for index, module_name in enumerate(sorted(modules))}

    for module_name in sorted(modules):
        module = modules[module_name]
        node = ET.SubElement(graph, graphml_tag("node"), id=module_ids[module_name])
        add_graphml_data(node, "node_label", format_node_label(module))
        add_graphml_data(node, "node_directory", "src" if module.directory == "." else module.directory)
        add_graphml_data(node, "node_header_loc", str(module.header_loc))
        add_graphml_data(node, "node_cpp_loc", str(module.cpp_loc))
        add_graphml_data(node, "node_total_loc", str(module.total_loc))
        add_graphml_data(node, "node_loc_annotation", format_module_loc_annotation(module))

    for index, ((source, target), kind) in enumerate(sorted(edges.items())):
        edge = ET.SubElement(
            graph,
            graphml_tag("edge"),
            id=f"e{index}",
            source=module_ids[source],
            target=module_ids[target],
        )
        add_graphml_data(edge, "edge_label", kind)
        add_graphml_data(edge, "edge_kind", kind)

    ET.indent(root, space="  ")
    ET.ElementTree(root).write(output_path, encoding="utf-8", xml_declaration=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Write and lint DOT and GraphML graphs of non-vendored src module dependencies."
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=DEFAULT_DOT_PATH,
        help=f"DOT output path. Defaults to {DEFAULT_DOT_PATH.relative_to(PROJECT_ROOT)}.",
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
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    files = list_project_source_files(SOURCE_ROOT)
    modules, module_by_file = collect_modules(files, SOURCE_ROOT)
    uses = collect_include_uses(files, SOURCE_ROOT, module_by_file)
    edges = merge_edges(uses)
    if any(target == EXTERNAL_D2D_MODULE for _, target in edges):
        modules.setdefault(EXTERNAL_D2D_MODULE, Module(name=EXTERNAL_D2D_MODULE, directory="external"))
    graphml_output = args.graphml_output or args.output.with_suffix(".graphml")
    svg_output = args.svg_output or args.output.with_suffix(".svg")
    write_dot(modules, edges, args.output)
    write_graphml(modules, edges, graphml_output)
    if not args.skip_svg:
        try:
            write_svg(args.output, svg_output)
        except RuntimeError as error:
            print(error, file=sys.stderr)
            return 1

    public_edges = sum(1 for kind in edges.values() if kind == "public")
    private_edges = sum(1 for kind in edges.values() if kind == "private")
    written_outputs = f"{args.output} and {graphml_output}"
    if not args.skip_svg:
        written_outputs += f", and {svg_output}"
    print(
        f"Wrote {written_outputs} with {len(modules)} modules, {public_edges} public dependencies, "
        f"and {private_edges} private dependencies."
    )
    print_package_loc_summary(modules)
    print_package_dependency_components(modules, edges)
    print_large_source_files(files)
    if args.check:
        violations = check_graph_rules(edges)
        if violations:
            print_violations(violations)
            print(f"Source dependency check failed with {len(violations)} violation(s).")
            return 1
        print("Source dependency check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
