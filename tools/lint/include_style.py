from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .common import (
    CheckResult,
    CheckerContext,
    Config,
    FileRecord,
    Finding,
    config_strings,
    has_root,
    is_excluded,
    normalize_include,
    project_path,
)


@dataclass(frozen=True)
class IncludeRoot:
    name: str
    path: Path


def create_checker(config: Config, context: CheckerContext) -> "IncludeStyleChecker":
    return IncludeStyleChecker(config, context)


class IncludeStyleChecker:
    def __init__(self, config: Config, context: CheckerContext) -> None:
        self.context = context
        self.roots = config_strings(config, "roots")
        self.suffixes = set(config_strings(config, "suffixes"))
        self.tracked_only = bool(config.get("tracked_only", False))
        self.nolint_message = str(config["nolint_message"])
        self.include_roots = tuple(
            IncludeRoot(name=root, path=project_path(context.project_root, root).resolve())
            for root in config_strings(config, "include_roots")
        )
        self.violations: list[Finding] = []
        self.checked_count = 0

    def process_file(self, record: FileRecord) -> None:
        if not self._is_eligible(record):
            return
        self.checked_count += 1
        for line_number in record.nolint_lines:
            self.violations.append(
                Finding(location=f"{record.relative}:{line_number}", kind="include-style", message=self.nolint_message)
            )

        for include in record.includes:
            if not include.quoted:
                continue
            resolved = self._resolve_project_include(record.path, include.text)
            if resolved is None:
                continue
            target_path, root_name = resolved
            expected = self._expected_include_for(target_path, root_name)
            normalized = normalize_include(include.text)
            if normalized == expected:
                continue
            self.violations.append(
                Finding(
                    location=f"{record.relative}:{include.line}",
                    kind="include-style",
                    message=(
                        f'Project header "{include.text}" resolves to {self._relpath(target_path)}; '
                        f'use "{expected}" from the {root_name} include root instead of a relative or local shorthand path.'
                    ),
                )
            )

    def finish(self, verbose: bool) -> CheckResult:
        del verbose
        self.violations.sort(key=lambda item: (item.location, item.message))
        summary = ""
        if self.violations:
            summary = f"Include style check failed with {len(self.violations)} violation(s)."
        return CheckResult(title="Include style check:", findings=tuple(self.violations), summary=summary)

    def _is_eligible(self, record: FileRecord) -> bool:
        if self.tracked_only and not record.tracked:
            return False
        if record.path.suffix not in self.suffixes:
            return False
        if self.roots and not has_root(record.relative, self.roots):
            return False
        return not is_excluded(record.relative, self.context.excluded_prefixes)

    def _resolve_project_include(self, current_file: Path, include_text: str) -> tuple[Path, str] | None:
        include_path = Path(normalize_include(include_text))
        candidates = [current_file.parent / include_path]
        candidates.extend(root.path / include_path for root in self.include_roots)

        for candidate in candidates:
            if not candidate.is_file():
                continue
            candidate_resolved = candidate.resolve()
            for root in self.include_roots:
                try:
                    candidate_resolved.relative_to(root.path)
                except ValueError:
                    continue
                candidate_rel = self._relpath(candidate_resolved)
                if is_excluded(candidate_rel, self.context.excluded_prefixes):
                    return None
                return candidate_resolved, root.name
        return None

    def _expected_include_for(self, path: Path, root_name: str) -> str:
        for root in self.include_roots:
            if root.name == root_name:
                return path.relative_to(root.path).as_posix()
        raise ValueError(f"Unknown include root {root_name}")

    def _relpath(self, path: Path) -> str:
        return path.relative_to(self.context.project_root).as_posix()
