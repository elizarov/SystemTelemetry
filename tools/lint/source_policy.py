from __future__ import annotations

import re
from dataclasses import dataclass
from re import Pattern

from .common import CheckResult, CheckerContext, Config, FileRecord, Finding, config_strings, has_root, is_excluded, suffix_group


@dataclass(frozen=True)
class LineRule:
    kind: str
    pattern: Pattern[str]
    message: str
    capture_group: int | None
    allowed_matches_by_file: dict[str, set[str]]


def create_checker(config: Config, context: CheckerContext) -> "SourcePolicyChecker":
    return SourcePolicyChecker(config, context)


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


def is_allowed_const_wide_string_literal(
    text: str,
    prefix_index: int,
    literal_index: int,
    end_index: int,
    allowed_declaration_pattern: Pattern[str],
) -> bool:
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
    return allowed_declaration_pattern.match(line[: prefix_index - line_start]) is not None


def find_undocumented_wide_literal_lines(text: str, allowed_declaration_pattern: Pattern[str]) -> list[int]:
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
                if not is_allowed_const_wide_string_literal(
                    text, index, literal_index, end, allowed_declaration_pattern
                ):
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


class SourcePolicyChecker:
    def __init__(self, config: Config, context: CheckerContext) -> None:
        self.context = context
        self.roots = config_strings(config, "roots")
        self.suffixes = suffix_group(context, str(config["suffix_group"]))
        self.guardrails_doc = str(config.get("guardrails_doc", ""))
        self.line_rules = tuple(self._parse_rule(rule) for rule in config.get("line_rules", []))
        wide_literal_config = config.get("wide_literals", {})
        self.wide_literal_allowance = re.compile(str(wide_literal_config["allowed_const_declaration_pattern"]))
        self.wide_literal_message = str(wide_literal_config.get("message", ""))
        self.violations: list[Finding] = []
        self.checked_count = 0

    def process_file(self, record: FileRecord) -> None:
        if not self._is_eligible(record):
            return
        self.checked_count += 1
        for line_number, line in enumerate(record.stripped_lines, start=1):
            for rule in self.line_rules:
                for match in rule.pattern.finditer(line):
                    if rule.capture_group is not None:
                        matched_text = match.group(rule.capture_group)
                        if matched_text in rule.allowed_matches_by_file.get(record.relative, set()):
                            continue
                    self.violations.append(
                        Finding(
                            location=f"{record.relative}:{line_number}",
                            kind=rule.kind,
                            message=self._format_message(rule.message, match),
                        )
                    )
                    if rule.capture_group is None:
                        break

        for line_number in find_undocumented_wide_literal_lines(record.text, self.wide_literal_allowance):
            self.violations.append(
                Finding(
                    location=f"{record.relative}:{line_number}",
                    kind="source-policy",
                    message=self._format_message(self.wide_literal_message, None),
                )
            )

    def finish(self, verbose: bool) -> CheckResult:
        del verbose
        self.violations.sort(key=lambda item: (item.location, item.message))
        summary = ""
        if self.violations:
            summary = f"Source policy check failed with {len(self.violations)} violation(s)."
        return CheckResult(title="Source policy check:", findings=tuple(self.violations), summary=summary)

    def _parse_rule(self, rule: Config) -> LineRule:
        allowed_matches_by_file = {
            str(path): {str(item) for item in values}
            for path, values in rule.get("allowed_matches_by_file", {}).items()
        }
        capture_group = rule.get("capture_group")
        return LineRule(
            kind=str(rule.get("kind", "source-policy")),
            pattern=re.compile(str(rule["pattern"])),
            message=str(rule["message"]),
            capture_group=int(capture_group) if capture_group is not None else None,
            allowed_matches_by_file=allowed_matches_by_file,
        )

    def _is_eligible(self, record: FileRecord) -> bool:
        if record.path.suffix not in self.suffixes:
            return False
        if self.roots and not has_root(record.relative, self.roots):
            return False
        return not is_excluded(record.relative, self.context.excluded_prefixes)

    def _format_message(self, template: str, match: re.Match[str] | None) -> str:
        values = {"guardrails_doc": self.guardrails_doc}
        if match is not None:
            values["match"] = match.group(0)
            for index, group in enumerate(match.groups(), start=1):
                values[f"match{index}"] = group
        return template.format(**values)
