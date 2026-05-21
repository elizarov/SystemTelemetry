from __future__ import annotations

import subprocess
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

from .common import (
    CheckResult,
    CheckerContext,
    Config,
    FileRecord,
    Finding,
    IncludeDirective,
    config_strings,
    format_count,
    has_root,
    is_excluded,
    normalize_include,
    project_path,
)


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
class IncludeUse:
    source: str
    target: str
    kind: str


@dataclass(frozen=True)
class PendingInclude:
    source_module: str
    source_file: Path
    include: IncludeDirective
    kind: str


@dataclass(frozen=True)
class LargeFile:
    relative: str
    loc: int


@dataclass(frozen=True)
class ExternalModule:
    name: str
    directory: str
    include_names: frozenset[str]
    allowed_packages: frozenset[str]
    violation_kind: str
    violation_message: str


def create_checker(config: Config, context: CheckerContext) -> "SourceDependencyChecker":
    return SourceDependencyChecker(config, context)


class SourceDependencyChecker:
    def __init__(self, config: Config, context: CheckerContext) -> None:
        self.context = context
        self.config = config
        self.source_root = project_path(context.project_root, str(config["source_root"])).resolve()
        self.root_label = str(config.get("root_label", self.source_root.name))
        self.roots = config_strings(config, "roots")
        self.suffixes = set(config_strings(config, "suffixes"))
        self.header_suffixes = set(config_strings(config, "header_suffixes"))
        self.large_source_file_loc_threshold = int(config.get("large_source_file_loc_threshold", 1000))
        self.graphml_namespace = str(config.get("graphml_namespace", "http://graphml.graphdrawing.org/xmlns"))
        self.dot_label = str(config.get("dot_label", "Source dependencies"))
        self.universal_package_dependencies = set(config_strings(config, "universal_package_dependencies"))
        self.package_dependency_limits = {
            str(package): {str(dependency) for dependency in dependencies}
            for package, dependencies in config.get("package_dependency_limits", {}).items()
        }
        self.package_encapsulation_message = str(config["package_encapsulation_message"])
        self.package_dependency_message = str(config["package_dependency_message"])
        self.external_modules = tuple(self._parse_external_module(item) for item in config.get("external_modules", []))
        self.external_module_by_include = {
            include_name: external.name
            for external in self.external_modules
            for include_name in external.include_names
        }
        self.external_module_by_name = {external.name: external for external in self.external_modules}
        self.modules: dict[str, Module] = {}
        self.module_by_file: dict[Path, str] = {}
        self.uses: list[IncludeUse] = []
        self.pending_includes: list[PendingInclude] = []
        self.large_files: list[LargeFile] = []
        self.edges: dict[tuple[str, str], str] = {}

    def process_file(self, record: FileRecord) -> None:
        if not self._is_eligible(record):
            return
        try:
            module_name = self._module_name_for(record.path)
        except ValueError:
            return
        module = self.modules.setdefault(module_name, Module(name=module_name, directory=self._module_directory(module_name)))
        module.files.add(record.path)
        if record.path.suffix in self.header_suffixes:
            module.header_files += 1
            module.header_loc += record.line_count
        else:
            module.cpp_files += 1
            module.cpp_loc += record.line_count
        self.module_by_file[record.path.resolve()] = module_name
        if record.line_count > self.large_source_file_loc_threshold:
            self.large_files.append(LargeFile(relative=record.relative, loc=record.line_count))

        kind = "public" if record.path.suffix in self.header_suffixes else "private"
        for include in record.includes:
            normalized = normalize_include(include.text).lower()
            external_module = self.external_module_by_include.get(normalized)
            if external_module is not None:
                self.uses.append(IncludeUse(source=module_name, target=external_module, kind=kind))
                continue
            self.pending_includes.append(
                PendingInclude(source_module=module_name, source_file=record.path, include=include, kind=kind)
            )

    def finish(self, verbose: bool) -> CheckResult:
        self._resolve_pending_includes()
        self.edges = self._merge_edges(self.uses)
        for _source, target in self.edges:
            external = self.external_module_by_name.get(target)
            if external is not None:
                self.modules.setdefault(target, Module(name=target, directory=external.directory))

        errors = list(self._write_outputs())
        package_order, cycle_error = self._topological_package_order()
        if cycle_error:
            errors.append(cycle_error)

        findings: list[Finding] = []
        if self.context.check_dependencies:
            findings.extend(self._check_graph_rules())
        findings.sort(key=lambda item: (item.location, item.kind, item.message))

        summary = ""
        if findings:
            summary = f"Source dependency check failed with {len(findings)} violation(s)."
        verbose_lines = tuple(self._verbose_lines(package_order)) if verbose else ()
        return CheckResult(
            title="Source dependency check:",
            findings=tuple(findings),
            errors=tuple(errors),
            summary=summary,
            verbose_lines=verbose_lines,
        )

    def _is_eligible(self, record: FileRecord) -> bool:
        if record.path.suffix not in self.suffixes:
            return False
        if self.roots and not has_root(record.relative, self.roots):
            return False
        return not is_excluded(record.relative, self.context.excluded_prefixes)

    def _parse_external_module(self, item: Config) -> ExternalModule:
        return ExternalModule(
            name=str(item["name"]),
            directory=str(item.get("directory", "external")),
            include_names=frozenset(str(name).lower() for name in item.get("include_names", [])),
            allowed_packages=frozenset(str(package) for package in item.get("allowed_packages", [])),
            violation_kind=str(item["violation_kind"]),
            violation_message=str(item["violation_message"]),
        )

    def _module_name_for(self, path: Path) -> str:
        return path.resolve().relative_to(self.source_root).with_suffix("").as_posix()

    def _module_directory(self, module_name: str) -> str:
        directory = Path(module_name).parent.as_posix()
        return "." if directory == "." else directory

    def _resolve_pending_includes(self) -> None:
        for pending in self.pending_includes:
            target_module = self._resolve_include(pending.source_file, pending.include.text)
            if target_module is None or target_module == pending.source_module:
                continue
            self.uses.append(IncludeUse(source=pending.source_module, target=target_module, kind=pending.kind))

    def _resolve_include(self, current_file: Path, include_text: str) -> str | None:
        normalized = normalize_include(include_text)
        candidates = [
            current_file.parent / normalized,
            self.source_root / normalized,
            self.context.project_root / normalized,
        ]
        for candidate in candidates:
            if not candidate.is_file():
                continue
            return self.module_by_file.get(candidate.resolve())
        return None

    def _merge_edges(self, uses: list[IncludeUse]) -> dict[tuple[str, str], str]:
        edges: dict[tuple[str, str], str] = {}
        for use in uses:
            key = (use.source, use.target)
            previous = edges.get(key)
            if previous == "public" or use.kind == previous:
                continue
            edges[key] = "public" if use.kind == "public" else "private"
        return edges

    def _write_outputs(self) -> tuple[str, ...]:
        try:
            self._write_dot(self.context.dot_output)
            self._write_graphml(self.context.graphml_output)
            if not self.context.skip_svg:
                self._write_svg(self.context.dot_output, self.context.svg_output)
        except (OSError, RuntimeError) as error:
            return (f"Source dependency graph write failed: {error}",)
        return ()

    def _top_level_package(self, module_name: str) -> str:
        return module_name.split("/", 1)[0]

    def _is_package_private_module(self, module_name: str) -> bool:
        return len(module_name.split("/")) > 2

    def _check_graph_rules(self) -> list[Finding]:
        return [
            *self._check_package_encapsulation(),
            *self._check_package_dependency_limits(),
            *self._check_external_module_limits(),
        ]

    def _check_package_encapsulation(self) -> list[Finding]:
        violations: list[Finding] = []
        for source, target in sorted(self.edges):
            if not self._is_package_private_module(target):
                continue
            source_package = self._top_level_package(source)
            target_package = self._top_level_package(target)
            if source_package == target_package:
                continue
            violations.append(
                Finding(
                    location=f"{source} -> {target}",
                    kind="package-encapsulation",
                    message=self.package_encapsulation_message.format(
                        source=source,
                        target=target,
                        source_package=source_package,
                        target_package=target_package,
                        target_package_root=f"{self.root_label}/{target_package}",
                    ),
                )
            )
        return violations

    def _check_package_dependency_limits(self) -> list[Finding]:
        violations: list[Finding] = []
        for source, target in sorted(self.edges):
            if target in self.external_module_by_name:
                continue
            source_package = self._top_level_package(source)
            target_package = self._top_level_package(target)
            allowed_targets = self.package_dependency_limits.get(source_package)
            if allowed_targets is None:
                continue
            if target_package in self.universal_package_dependencies and source_package not in self.universal_package_dependencies:
                continue
            if target_package == source_package or target_package in allowed_targets:
                continue
            violations.append(
                Finding(
                    location=f"{source} -> {target}",
                    kind=f"package-dependency-{source_package}",
                    message=self.package_dependency_message.format(
                        source=source,
                        target=target,
                        source_package=source_package,
                        target_package=target_package,
                        allowed_dependencies=self._format_allowed_package_dependencies(source_package, allowed_targets),
                    ),
                )
            )
        return violations

    def _check_external_module_limits(self) -> list[Finding]:
        violations: list[Finding] = []
        for source, target in sorted(self.edges):
            external = self.external_module_by_name.get(target)
            if external is None:
                continue
            source_package = self._top_level_package(source)
            if source_package in external.allowed_packages:
                continue
            violations.append(
                Finding(
                    location=f"{source} -> {target}",
                    kind=external.violation_kind,
                    message=external.violation_message.format(
                        source=source,
                        target=target,
                        source_package=source_package,
                        allowed_packages=", ".join(sorted(external.allowed_packages)),
                    ),
                )
            )
        return violations

    def _format_allowed_package_dependencies(self, package: str, dependencies: set[str]) -> str:
        universal = set() if package in self.universal_package_dependencies else self.universal_package_dependencies
        allowed = [package, *sorted(dependencies | universal)]
        if len(allowed) == 1:
            return f"{allowed[0]} modules"
        return ", ".join(allowed[:-1]) + f", or {allowed[-1]} modules"

    def _package_dependencies(self) -> dict[str, set[str]]:
        dependencies: dict[str, set[str]] = {
            self._top_level_package(module.name): set()
            for module in self.modules.values()
            if module.name not in self.external_module_by_name
        }
        for source, target in self.edges:
            if source in self.external_module_by_name or target in self.external_module_by_name:
                continue
            source_package = self._top_level_package(source)
            target_package = self._top_level_package(target)
            dependencies.setdefault(source_package, set())
            dependencies.setdefault(target_package, set())
            if source_package != target_package:
                dependencies[source_package].add(target_package)
        return dependencies

    def _topological_package_order(self) -> tuple[list[str], str | None]:
        graph = self._package_dependencies()
        indegrees = {package: 0 for package in graph}
        for targets in graph.values():
            for target in targets:
                indegrees[target] = indegrees.get(target, 0) + 1
        ready = sorted(package for package, indegree in indegrees.items() if indegree == 0)
        ordered: list[str] = []
        while ready:
            package = ready.pop(0)
            ordered.append(package)
            for target in sorted(graph.get(package, set())):
                indegrees[target] -= 1
                if indegrees[target] == 0:
                    ready.append(target)
            ready.sort()
        if len(ordered) == len(indegrees):
            return ordered, None
        cyclic = ", ".join(sorted(package for package in indegrees if package not in ordered))
        return ordered, f"Package dependency graph is not a DAG; cycle detected among: {cyclic}."

    def _summarize_package_loc(self) -> dict[str, PackageLocSummary]:
        summaries: dict[str, PackageLocSummary] = {}
        for module in self.modules.values():
            if module.name in self.external_module_by_name:
                continue
            package = self._top_level_package(module.name)
            summary = summaries.setdefault(package, PackageLocSummary())
            summary.header_files += module.header_files
            summary.header_loc += module.header_loc
            summary.cpp_files += module.cpp_files
            summary.cpp_loc += module.cpp_loc
        return summaries

    def _verbose_lines(self, package_order: list[str]) -> list[str]:
        lines: list[str] = []
        if self.large_files:
            lines.append(f"Source files over {format_count(self.large_source_file_loc_threshold)} LOC:")
            for source_file in sorted(self.large_files, key=lambda item: (-item.loc, item.relative)):
                lines.append(f"  {source_file.relative}: {format_count(source_file.loc)} LOC")
        lines.append("")
        lines.append("Package dependencies in topological order:")
        summaries = self._summarize_package_loc()
        graph = self._package_dependencies()
        for index, package in enumerate(package_order, start=1):
            dependencies = ", ".join(sorted(graph.get(package, set()))) if graph.get(package) else "(none)"
            total_loc = summaries.get(package, PackageLocSummary()).total_loc
            lines.append(f"  {index}. {format_count(total_loc)} LOC: {package} -> {dependencies}")
        return lines

    def _format_module_loc_annotation(self, module: Module) -> str:
        return (
            f".h {format_count(module.header_loc)} LOC | "
            f".cpp {format_count(module.cpp_loc)} LOC | "
            f"total {format_count(module.total_loc)} LOC"
        )

    def _format_node_label(self, module: Module) -> str:
        return f"{self._display_name(module.name)}\n{self._format_module_loc_annotation(module)}"

    def _dot_quote(self, text: str) -> str:
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'

    def _display_name(self, module_name: str) -> str:
        return module_name.replace("/", "\\")

    def _cluster_id(self, directory: str) -> str:
        if directory == ".":
            safe_root = "".join(ch if ch.isalnum() else "_" for ch in self.root_label)
            return f"cluster_{safe_root or 'root'}"
        safe = "".join(ch if ch.isalnum() else "_" for ch in directory)
        return f"cluster_{safe or 'root'}"

    def _write_dot(self, output_path: Path) -> None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        modules_by_directory: dict[str, list[Module]] = {}
        for module in self.modules.values():
            modules_by_directory.setdefault(module.directory, []).append(module)

        lines = [
            "digraph SourceDependencies {",
            f"  graph [rankdir=LR, compound=true, newrank=true, fontname=\"Segoe UI\", labelloc=t, label={self._dot_quote(self.dot_label)}];",
            "  node [shape=box, style=\"rounded,filled\", fillcolor=\"#f8fafc\", color=\"#64748b\", fontname=\"Segoe UI\", fontsize=10];",
            "  edge [fontname=\"Segoe UI\", fontsize=9, arrowsize=0.7];",
            "",
        ]

        for directory in sorted(modules_by_directory):
            label = self.root_label if directory == "." else directory.replace("/", "\\")
            lines.extend(
                [
                    f"  subgraph {self._cluster_id(directory)} {{",
                    f"    label={self._dot_quote(label)};",
                    "    color=\"#cbd5e1\";",
                    "    style=\"rounded\";",
                ]
            )
            for module in sorted(modules_by_directory[directory], key=lambda item: item.name):
                lines.append(f"    {self._dot_quote(module.name)} [label={self._dot_quote(self._format_node_label(module))}];")
            lines.extend(["  }", ""])

        for (source, target), kind in sorted(self.edges.items()):
            if kind == "public":
                attributes = 'label="public", color="#2563eb", fontcolor="#2563eb", penwidth=1.6'
            else:
                attributes = 'label="private", color="#64748b", fontcolor="#64748b", style="dashed"'
            lines.append(f"  {self._dot_quote(source)} -> {self._dot_quote(target)} [{attributes}];")

        lines.append("}")
        output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def _write_svg(self, dot_path: Path, output_path: Path) -> None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            subprocess.run(
                ["dot", "-Tsvg", str(dot_path), "-o", str(output_path)],
                cwd=self.context.project_root,
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

    def _graphml_tag(self, name: str) -> str:
        return f"{{{self.graphml_namespace}}}{name}"

    def _add_graphml_data(self, parent: ET.Element, key: str, value: str) -> None:
        data = ET.SubElement(parent, self._graphml_tag("data"), key=key)
        data.text = value

    def _write_graphml(self, output_path: Path) -> None:
        output_path.parent.mkdir(parents=True, exist_ok=True)

        ET.register_namespace("", self.graphml_namespace)
        root = ET.Element(self._graphml_tag("graphml"))
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="node_label",
            **{"for": "node", "attr.name": "label", "attr.type": "string"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="node_directory",
            **{"for": "node", "attr.name": "directory", "attr.type": "string"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="node_header_loc",
            **{"for": "node", "attr.name": "header_loc", "attr.type": "int"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="node_cpp_loc",
            **{"for": "node", "attr.name": "cpp_loc", "attr.type": "int"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="node_total_loc",
            **{"for": "node", "attr.name": "total_loc", "attr.type": "int"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="node_loc_annotation",
            **{"for": "node", "attr.name": "loc_annotation", "attr.type": "string"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="edge_label",
            **{"for": "edge", "attr.name": "label", "attr.type": "string"},
        )
        ET.SubElement(
            root,
            self._graphml_tag("key"),
            id="edge_kind",
            **{"for": "edge", "attr.name": "kind", "attr.type": "string"},
        )

        graph = ET.SubElement(root, self._graphml_tag("graph"), id="SourceDependencies", edgedefault="directed")
        module_ids = {module_name: f"n{index}" for index, module_name in enumerate(sorted(self.modules))}

        for module_name in sorted(self.modules):
            module = self.modules[module_name]
            node = ET.SubElement(graph, self._graphml_tag("node"), id=module_ids[module_name])
            self._add_graphml_data(node, "node_label", self._format_node_label(module))
            self._add_graphml_data(node, "node_directory", self.root_label if module.directory == "." else module.directory)
            self._add_graphml_data(node, "node_header_loc", str(module.header_loc))
            self._add_graphml_data(node, "node_cpp_loc", str(module.cpp_loc))
            self._add_graphml_data(node, "node_total_loc", str(module.total_loc))
            self._add_graphml_data(node, "node_loc_annotation", self._format_module_loc_annotation(module))

        for index, ((source, target), kind) in enumerate(sorted(self.edges.items())):
            edge = ET.SubElement(
                graph,
                self._graphml_tag("edge"),
                id=f"e{index}",
                source=module_ids[source],
                target=module_ids[target],
            )
            self._add_graphml_data(edge, "edge_label", kind)
            self._add_graphml_data(edge, "edge_kind", kind)

        ET.indent(root, space="  ")
        ET.ElementTree(root).write(output_path, encoding="utf-8", xml_declaration=True)
