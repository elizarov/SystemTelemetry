from __future__ import annotations

import re
from dataclasses import dataclass

from .common import CheckResult, CheckerContext, Config, FileRecord, Finding, config_strings, has_root, is_excluded


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
class Definition:
    relpath: str
    line: int
    name: str
    owner_name: str
    qualified: bool


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


def namespace_stack_for_line(line: str, namespace_stack: list[str]) -> list[str]:
    namespace_match = re.fullmatch(r"\s*(?:inline\s+)?namespace\s+([A-Za-z_]\w*)\s*\{\s*", line)
    if namespace_match:
        namespace_stack.append(namespace_match.group(1))
        return namespace_stack
    if re.fullmatch(r"\s*}\s*(?://.*)?", line) and namespace_stack:
        namespace_stack.pop()
    return namespace_stack


def create_checker(config: Config, context: CheckerContext) -> "ArchitectureChecker":
    return ArchitectureChecker(config, context)


class ArchitectureChecker:
    def __init__(self, config: Config, context: CheckerContext) -> None:
        self.context = context
        self.roots = config_strings(config, "roots")
        self.header_suffix = str(config.get("header_suffix", ".h"))
        self.implementation_suffix = str(config.get("implementation_suffix", ".cpp"))
        self.header_body_allowlist = set(config_strings(config, "header_body_allowlist"))
        self.cpp_without_header_allowlist = set(config_strings(config, "cpp_without_header_allowlist"))
        self.control_keywords = set(config_strings(config, "control_keywords"))
        self.class_headers: dict[str, set[str]] = {}
        self.free_headers: dict[str, set[str]] = {}
        self.definitions: list[Definition] = []
        self.violations: list[Finding] = []
        self.header_count = 0
        self.cpp_count = 0

    def process_file(self, record: FileRecord) -> None:
        if not self._is_eligible(record):
            return
        if record.path.suffix == self.header_suffix:
            self.header_count += 1
            self._process_header(record)
        elif record.path.suffix == self.implementation_suffix:
            self.cpp_count += 1
            self._process_implementation(record)

    def finish(self, verbose: bool) -> CheckResult:
        del verbose
        self._collect_definition_violations()
        self.violations.sort(key=lambda item: (item.location, item.kind, item.message))
        summary = ""
        if self.violations:
            summary = f"Architecture check failed with {len(self.violations)} violation(s)."
        return CheckResult(title="Architecture check:", findings=tuple(self.violations), summary=summary)

    def _is_eligible(self, record: FileRecord) -> bool:
        if record.path.suffix not in {self.header_suffix, self.implementation_suffix}:
            return False
        if self.roots and not has_root(record.relative, self.roots):
            return False
        return not is_excluded(record.relative, self.context.excluded_prefixes)

    def _process_header(self, record: FileRecord) -> None:
        namespace_stack: list[str] = []
        for raw_line in record.stripped_lines:
            namespace_stack_for_line(raw_line, namespace_stack)
            for match in CLASS_DECL_RE.finditer(raw_line):
                name = match.group(1)
                suffix = raw_line[match.end() :]
                if "{" not in suffix:
                    continue
                self.class_headers.setdefault(name, set()).add(record.relative)
                if namespace_stack:
                    self.class_headers.setdefault("::".join([*namespace_stack, name]), set()).add(record.relative)

        for _line_number, statement, terminator in collect_top_level_statements(record.stripped_text):
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
            if name in self.control_keywords:
                continue
            self.free_headers.setdefault(name, set()).add(record.relative)

        if record.relative in self.header_body_allowlist:
            return
        for match in HEADER_BODY_RE.finditer(record.stripped_text):
            name = match.group(1)
            if name in self.control_keywords:
                continue
            prefix = record.stripped_text[max(0, match.start() - 80) : match.start()]
            if "template <" in prefix or "template<" in prefix:
                continue
            line_number = record.stripped_text.count("\n", 0, match.start()) + 1
            self.violations.append(
                Finding(
                    location=f"{record.relative}:{line_number}",
                    kind="header-body",
                    message=f"Function body for {name} appears in a header; move non-template logic to a matching .cpp file.",
                )
            )

    def _process_implementation(self, record: FileRecord) -> None:
        if record.relative not in self.cpp_without_header_allowlist:
            expected_header = self.context.project_root / self._paired_header_for_cpp(record.relative)
            if not expected_header.exists():
                self.violations.append(
                    Finding(
                        location=f"{record.relative}:1",
                        kind="missing-header",
                        message=(
                            f"{record.relative} has no matching header {self._paired_header_for_cpp(record.relative)}; "
                            "add one or allowlist the translation unit."
                        ),
                    )
                )

        for line_number, statement, terminator in collect_top_level_statements(record.stripped_text):
            if terminator != "{":
                continue
            if "operator" in statement:
                continue
            qualified = QUALIFIED_DEF_RE.search(statement)
            if qualified:
                full_name = qualified.group(1)
                self.definitions.append(
                    Definition(
                        relpath=record.relative,
                        line=line_number,
                        name=full_name,
                        owner_name=full_name.rsplit("::", 1)[0],
                        qualified=True,
                    )
                )
                continue

            match = FREE_DECL_RE.search(statement)
            if not match:
                continue
            name = match.group(1)
            if name in self.control_keywords:
                continue
            self.definitions.append(
                Definition(relpath=record.relative, line=line_number, name=name, owner_name=name, qualified=False)
            )

    def _collect_definition_violations(self) -> None:
        for definition in self.definitions:
            owners = self.class_headers.get(definition.owner_name) if definition.qualified else self.free_headers.get(definition.name)
            if not owners or len(owners) != 1:
                continue
            owner_header = next(iter(owners))
            expected_cpp = self._paired_cpp_for_header(owner_header)
            if definition.relpath == expected_cpp:
                continue
            subject = definition.name
            self.violations.append(
                Finding(
                    location=f"{definition.relpath}:{definition.line}",
                    kind="impl-mismatch",
                    message=(
                        f"{subject} is declared from {owner_header} but implemented in "
                        f"{definition.relpath}; expected {expected_cpp}."
                    ),
                )
            )

    def _paired_cpp_for_header(self, header_rel: str) -> str:
        return header_rel[: -len(self.header_suffix)] + self.implementation_suffix

    def _paired_header_for_cpp(self, cpp_rel: str) -> str:
        return cpp_rel[: -len(self.implementation_suffix)] + self.header_suffix
