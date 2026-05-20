#!/usr/bin/env python3
"""Generate CaseDash config metadata from src/config/config.h."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


SUPPORTED_VALUE_TYPES = {
    "int": ("Int", "Integer", "positive_int"),
    "double": ("Double", "FloatingPoint", "none"),
    "std::string": ("String", "String", "none"),
    "std::vector<std::string>": ("StringList", "String", "none"),
    "LogicalPointConfig": ("LogicalPoint", "String", "none"),
    "LogicalSizeConfig": ("LogicalSize", "String", "none"),
    "ColorConfig": ("HexColor", "ColorHex", "none"),
    "UiFontConfig": ("FontSpec", "FontSpec", "font_size"),
    "LayoutNodeConfig": ("LayoutExpression", "String", "none"),
}

POLICY_CODE = {
    "none": "None",
    "positive_int": "PositiveInt",
    "non_negative_int": "NonNegativeInt",
    "font_size": "FontSize",
    "degrees": "Degrees",
}

class ConfigMetaError(RuntimeError):
    pass


@dataclass
class Field:
    type_name: str
    name: str
    runtime_only: bool = False
    policy: str | None = None
    key_override: str | None = None
    line: int = 0

    @property
    def key(self) -> str:
        return self.key_override or lower_camel_to_snake(self.name)


@dataclass
class StructDesc:
    name: str
    kind: str
    fields: list[Field] = field(default_factory=list)
    section: str | None = None
    pattern: str | None = None
    prefix: str | None = None
    key_member: str | None = None
    codec: str | None = None
    line: int = 0
    has_equality_operator: bool = False
    equality_line: int = 0

    def config_fields(self) -> list[Field]:
        return [
            item
            for item in self.fields
            if not item.runtime_only and not (self.kind == "dynamic" and item.name == self.key_member)
        ]


def lower_camel_to_snake(name: str) -> str:
    out: list[str] = []
    for index, ch in enumerate(name):
        prev = name[index - 1] if index > 0 else ""
        next_ch = name[index + 1] if index + 1 < len(name) else ""
        if ch.isupper() and index > 0 and (prev.islower() or prev.isdigit() or (prev.isupper() and next_ch.islower())):
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def vector_item_type(type_name: str) -> str | None:
    match = re.fullmatch(r"std::vector<\s*([A-Za-z_][A-Za-z0-9_:]*)\s*>", type_name)
    return match.group(1) if match else None


def parse_field_attrs(text: str | None, line_number: int) -> tuple[bool, str | None, str | None]:
    runtime_only = False
    policy = None
    key_override = None
    if not text:
        return runtime_only, policy, key_override
    for attr in text.split():
        if attr == "runtime_only":
            runtime_only = True
        elif attr.startswith("policy="):
            policy = attr.removeprefix("policy=")
            if policy not in POLICY_CODE:
                raise ConfigMetaError(f"line {line_number}: unknown policy '{policy}'")
        elif attr.startswith("rename="):
            key_override = attr.removeprefix("rename=")
            if not re.fullmatch(r"[a-z][a-z0-9_]*", key_override):
                raise ConfigMetaError(f"line {line_number}: invalid renamed config key '{key_override}'")
        else:
            raise ConfigMetaError(f"line {line_number}: unknown field attribute '{attr}'")
    if runtime_only and key_override is not None:
        raise ConfigMetaError(f"line {line_number}: runtime-only fields cannot declare rename=")
    return runtime_only, policy, key_override


def parse_struct_directive(text: str, line_number: int) -> dict[str, str]:
    if text == "container":
        return {"kind": "container"}
    if text == "root":
        return {"kind": "root"}
    match = re.fullmatch(r"static\s+\[([A-Za-z0-9_.]+)\]", text)
    if match:
        return {"kind": "static", "section": match.group(1)}
    match = re.fullmatch(r"dynamic_section\s+\[([A-Za-z0-9_.]*)\$([A-Za-z_][A-Za-z0-9_]*)\]\s+key=([A-Za-z_][A-Za-z0-9_]*)", text)
    if match:
        literal_key = match.group(2)
        key_member = match.group(3)
        if literal_key != key_member:
            raise ConfigMetaError(
                f"line {line_number}: dynamic section suffix '${literal_key}' must match key={key_member}"
            )
        return {
            "kind": "dynamic",
            "pattern": f"[{match.group(1)}${literal_key}]",
            "prefix": match.group(1),
            "key_member": key_member,
        }
    match = re.fullmatch(
        r"custom_section\s+\[([A-Za-z0-9_.]+)\]\s+codec=([A-Za-z_][A-Za-z0-9_]*)", text
    )
    if match:
        codec = match.group(2)
        if not codec.endswith("SectionCodec"):
            raise ConfigMetaError(
                f"line {line_number}: custom section codec '{codec}' must end with SectionCodec"
            )
        return {"kind": "custom", "section": match.group(1), "codec": codec}
    raise ConfigMetaError(f"line {line_number}: malformed struct directive '{text}'")


def parse_descriptor(path: Path) -> list[StructDesc]:
    structs: list[StructDesc] = []
    pending_directive: dict[str, str] | None = None
    active: StructDesc | None = None
    namespace_re = re.compile(r"namespace\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_][A-Za-z0-9_:]*\s*;")
    empty_struct_re = re.compile(r"struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{\s*\};")
    struct_re = re.compile(r"struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{")
    field_re = re.compile(
        r"([A-Za-z_][A-Za-z0-9_:]*(?:\s*<\s*[A-Za-z_][A-Za-z0-9_:]*\s*>)?)\s+"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\{\})?\s*;\s*(?://\s*config_meta:\s*(.*))?$"
    )
    equality_re = re.compile(
        r"bool\s+operator==\(const\s+([A-Za-z_][A-Za-z0-9_]*)&\s+other\)\s+const\s+=\s+default;"
    )

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if active is None and line == "#pragma once":
            continue
        if active is None and line.startswith("#include "):
            continue
        if active is None and namespace_re.fullmatch(line):
            continue
        if line == "};":
            if active is None:
                raise ConfigMetaError(f"line {line_number}: stray struct close")
            structs.append(active)
            active = None
            continue
        if line.startswith("//"):
            marker = re.fullmatch(r"//\s*config_meta:\s*(.+)", line)
            if marker:
                if active is not None:
                    raise ConfigMetaError(f"line {line_number}: struct directive inside a struct")
                if pending_directive is not None:
                    raise ConfigMetaError(f"line {line_number}: directive does not apply to a struct")
                pending_directive = parse_struct_directive(marker.group(1).strip(), line_number)
            continue
        if "config_meta:" in line and not field_re.fullmatch(line):
            raise ConfigMetaError(f"line {line_number}: config_meta directive is not attached to a supported line")
        match = empty_struct_re.fullmatch(line)
        if match:
            if active is not None:
                raise ConfigMetaError(f"line {line_number}: nested structs are not supported")
            if pending_directive is None:
                raise ConfigMetaError(f"line {line_number}: struct is missing a config_meta directive")
            structs.append(StructDesc(name=match.group(1), line=line_number, **pending_directive))
            pending_directive = None
            continue
        match = struct_re.fullmatch(line)
        if match:
            if active is not None:
                raise ConfigMetaError(f"line {line_number}: nested structs are not supported")
            if pending_directive is None:
                raise ConfigMetaError(f"line {line_number}: struct is missing a config_meta directive")
            active = StructDesc(name=match.group(1), line=line_number, **pending_directive)
            pending_directive = None
            continue
        if active is not None:
            match = equality_re.fullmatch(line)
            if match:
                if match.group(1) != active.name:
                    raise ConfigMetaError(
                        f"line {line_number}: equality operator type '{match.group(1)}' does not match struct "
                        f"'{active.name}'"
                    )
                if active.has_equality_operator:
                    raise ConfigMetaError(
                        f"line {line_number}: duplicate equality operator in struct '{active.name}'"
                    )
                active.has_equality_operator = True
                active.equality_line = line_number
                continue
            match = field_re.fullmatch(line)
            if not match:
                raise ConfigMetaError(f"line {line_number}: unsupported field declaration '{line}'")
            if active.has_equality_operator:
                raise ConfigMetaError(
                    f"line {line_number}: field declaration appears after equality operator in struct '{active.name}'"
                )
            runtime_only, policy, key_override = parse_field_attrs(match.group(3), line_number)
            type_name = re.sub(r"\s+", "", match.group(1))
            active.fields.append(
                Field(
                    type_name=type_name,
                    name=match.group(2),
                    runtime_only=runtime_only,
                    policy=policy,
                    key_override=key_override,
                    line=line_number,
                )
            )
            continue
        raise ConfigMetaError(f"line {line_number}: unsupported descriptor line '{line}'")

    if active is not None:
        raise ConfigMetaError(f"line {active.line}: struct '{active.name}' is not closed")
    if pending_directive is not None:
        raise ConfigMetaError("trailing config_meta directive does not apply to a struct")
    return structs


def parse_primitive_annotations(path: Path) -> list[StructDesc]:
    structs: list[StructDesc] = []
    pending_directive: dict[str, str] | None = None
    pending_line = 0
    struct_re = re.compile(r"struct\s+([A-Za-z_][A-Za-z0-9_]*)\b")

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        marker = re.fullmatch(r"//\s*config_meta:\s*(.+)", line)
        if marker:
            if pending_directive is not None:
                raise ConfigMetaError(f"{path}:{line_number}: directive does not apply to a struct")
            pending_directive = parse_struct_directive(marker.group(1).strip(), line_number)
            pending_line = line_number
            if pending_directive["kind"] != "custom":
                raise ConfigMetaError(f"{path}:{line_number}: primitive annotations only support custom_section")
            continue

        if pending_directive is None:
            continue

        match = struct_re.search(line)
        if match:
            structs.append(
                StructDesc(
                    name=match.group(1),
                    line=line_number,
                    has_equality_operator=True,
                    equality_line=line_number,
                    **pending_directive,
                )
            )
            pending_directive = None

    if pending_directive is not None:
        raise ConfigMetaError(f"{path}:{pending_line}: trailing config_meta directive does not apply to a struct")
    return structs


def identifier_words(name: str) -> list[str]:
    return [item for item in lower_camel_to_snake(name).split("_") if item]


def singular_word(word: str) -> str:
    if word.endswith("ies") and len(word) > 3:
        return f"{word[:-3]}y"
    if word.endswith("s") and len(word) > 1:
        return word[:-1]
    return word


def append_unique(items: list[list[str]], value: list[str]) -> None:
    if value and value not in items:
        items.append(value)


def config_type_words(type_name: str) -> list[str]:
    words = identifier_words(type_name)
    for suffix in (["widget", "config"], ["section", "config"], ["config"]):
        if words[-len(suffix) :] == suffix:
            return words[: -len(suffix)]
    return words


def layout_edit_owner_aliases(struct: StructDesc) -> list[list[str]]:
    aliases: list[list[str]] = []
    sources = [config_type_words(struct.name)]
    if struct.section is not None:
        sources.append(identifier_words(struct.section))

    for words in sources:
        append_unique(aliases, words)
        append_unique(aliases, words[:-1] + [singular_word(words[-1])])
        if words[-1:] in (["list"], ["style"]):
            append_unique(aliases, words[:-1])
            append_unique(aliases, words[:-2] + [singular_word(words[-2])] if len(words) > 1 else [])
    return aliases


def field_aliases(field: Field) -> list[list[str]]:
    aliases: list[list[str]] = []
    append_unique(aliases, identifier_words(field.name))
    append_unique(aliases, identifier_words(field.key))
    return aliases


def starts_with_words(words: list[str], prefix: list[str]) -> bool:
    return len(words) >= len(prefix) and words[: len(prefix)] == prefix


def ends_with_words(words: list[str], suffix: list[str]) -> bool:
    return len(words) >= len(suffix) and words[-len(suffix) :] == suffix


def layout_edit_field_suffixes(field_words: list[str], owner_words: list[str]) -> list[list[str]]:
    suffixes: list[list[str]] = []
    append_unique(suffixes, field_words)
    if starts_with_words(field_words, owner_words):
        append_unique(suffixes, field_words[len(owner_words) :])
    if ends_with_words(field_words, owner_words):
        append_unique(suffixes, field_words[: -len(owner_words)])
    return suffixes


def words_to_pascal(words: list[str]) -> str:
    return "".join(item[:1].upper() + item[1:] for item in words)


def parse_layout_edit_parameter_names(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        r"//\s*config_meta:\s*layout_enum\s*\n\s*enum\s+class\s+LayoutEditParameter\s*:\s*std::uint8_t\s*\{"
        r"(?P<body>.*?)\};",
        text,
        re.S,
    )
    if not match:
        raise ConfigMetaError(f"{path}: missing config_meta layout_enum annotation on LayoutEditParameter enum")

    names: list[str] = []
    body_start_line = text[: match.start("body")].count("\n") + 1
    for offset, raw_line in enumerate(match.group("body").splitlines(), start=0):
        line_number = body_start_line + offset
        line = raw_line.split("//", 1)[0].strip()
        if not line:
            continue
        item = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*,", line)
        if not item:
            raise ConfigMetaError(f"{path}:{line_number}: unsupported LayoutEditParameter entry '{line}'")
        name = item.group(1)
        if name == "Count":
            break
        names.append(name)

    if not names:
        raise ConfigMetaError(f"{path}: LayoutEditParameter enum has no generated metadata entries")
    return names


def build_layout_edit_candidate_map(structs: list[StructDesc]) -> dict[str, set[tuple[str, str]]]:
    candidates: dict[str, set[tuple[str, str]]] = {}
    for struct in structs:
        if struct.kind != "static":
            continue
        for owner_words in layout_edit_owner_aliases(struct):
            for field in struct.config_fields():
                for field_words in field_aliases(field):
                    for suffix_words in layout_edit_field_suffixes(field_words, owner_words):
                        candidate = words_to_pascal(owner_words + suffix_words)
                        candidates.setdefault(candidate, set()).add((struct.name, field.name))
    return candidates


def resolve_layout_edit_parameters(path: Path, structs: list[StructDesc]) -> list[tuple[str, str, str]]:
    candidates = build_layout_edit_candidate_map(structs)
    parameters: list[tuple[str, str, str]] = []
    for enum_name in parse_layout_edit_parameter_names(path):
        matches = sorted(candidates.get(enum_name, set()))
        if not matches:
            raise ConfigMetaError(f"layout edit parameter '{enum_name}' does not match a static config field")
        if len(matches) > 1:
            formatted = ", ".join(f"{owner_type}::{field_name}" for owner_type, field_name in matches)
            raise ConfigMetaError(f"layout edit parameter '{enum_name}' matches multiple config fields: {formatted}")
        owner_type, field_name = matches[0]
        parameters.append((enum_name, owner_type, field_name))
    return parameters


def parse_resource_config(path: Path) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    section: str | None = None
    for raw_line in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if not line or line[0] in "#;":
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
            sections.setdefault(section, [])
            continue
        if section is None:
            continue
        if "=" not in line:
            continue
        key = line.split("=", 1)[0].strip()
        sections[section].append(key)
    return sections


def validate_descriptor(structs: list[StructDesc], resource_sections: dict[str, list[str]]) -> None:
    by_name: dict[str, StructDesc] = {}
    section_names: set[str] = set()
    root_count = 0
    for struct in structs:
        if struct.name in by_name:
            raise ConfigMetaError(f"line {struct.line}: duplicate struct '{struct.name}'")
        by_name[struct.name] = struct
        if struct.kind != "custom" and not struct.has_equality_operator:
            raise ConfigMetaError(
                f"line {struct.line}: struct '{struct.name}' must end with a defaulted equality operator"
            )
        if struct.kind == "root":
            root_count += 1
        if struct.section is not None:
            if struct.section in section_names:
                raise ConfigMetaError(f"line {struct.line}: duplicate section '[{struct.section}]'")
            section_names.add(struct.section)
        if struct.kind == "dynamic":
            if struct.prefix in section_names:
                raise ConfigMetaError(f"line {struct.line}: duplicate dynamic section prefix '[{struct.prefix}$...]'")
            section_names.add(struct.prefix or "")
            if not any(item.name == struct.key_member and item.type_name == "std::string" for item in struct.fields):
                raise ConfigMetaError(
                    f"line {struct.line}: dynamic section '{struct.name}' is missing string key field '{struct.key_member}'"
                )

        keys: set[str] = set()
        for item in struct.config_fields():
            if item.key in keys:
                raise ConfigMetaError(f"line {item.line}: duplicate key '{item.key}' in struct '{struct.name}'")
            keys.add(item.key)
            if struct.kind in {"static", "dynamic"} and item.type_name not in SUPPORTED_VALUE_TYPES:
                raise ConfigMetaError(f"line {item.line}: unsupported config field type '{item.type_name}'")
            if item.policy is not None and item.policy not in POLICY_CODE:
                raise ConfigMetaError(f"line {item.line}: unknown policy '{item.policy}'")

    if root_count != 1:
        raise ConfigMetaError(f"descriptor must define exactly one root struct; found {root_count}")

    for struct in structs:
        if struct.kind not in {"container", "root"}:
            continue
        for item in struct.fields:
            if item.runtime_only:
                if item.type_name not in by_name:
                    raise ConfigMetaError(f"line {item.line}: runtime-only owner field has unknown type '{item.type_name}'")
                continue
            vector_item = vector_item_type(item.type_name)
            if vector_item is not None:
                if vector_item == "std::string":
                    continue
                target = by_name.get(vector_item)
                if target is None or target.kind != "dynamic":
                    raise ConfigMetaError(f"line {item.line}: vector field '{item.name}' must own a dynamic section type")
            elif item.type_name not in by_name:
                raise ConfigMetaError(f"line {item.line}: owner field '{item.name}' has unknown type '{item.type_name}'")

    for struct in structs:
        if struct.kind == "static":
            expected = [item.key for item in struct.config_fields()]
            actual = resource_sections.get(struct.section or "")
            if actual is None:
                raise ConfigMetaError(f"section '[{struct.section}]' is missing from resources/config.ini")
            if actual != expected:
                raise ConfigMetaError(
                    f"section '[{struct.section}]' keys do not match resources/config.ini: "
                    f"generated {expected}, resource {actual}"
                )
        elif struct.kind == "dynamic":
            expected = {item.key for item in struct.config_fields()}
            prefix = struct.prefix or ""
            actual_sections = [name for name in resource_sections if name.startswith(prefix)]
            if not actual_sections:
                raise ConfigMetaError(f"dynamic section '[{prefix}$...]' has no representative in resources/config.ini")
            actual: set[str] = set()
            for section in actual_sections:
                actual.update(resource_sections[section])
            if actual != expected:
                raise ConfigMetaError(
                    f"dynamic section '[{prefix}$...]' keys do not match resources/config.ini: "
                    f"generated {sorted(expected)}, resource union {sorted(actual)}"
                )
        elif struct.kind == "custom":
            if struct.section not in resource_sections:
                raise ConfigMetaError(f"custom section '[{struct.section}]' is missing from resources/config.ini")


def field_metadata(field: Field) -> dict[str, str]:
    kind, value_format, default_policy = SUPPORTED_VALUE_TYPES[field.type_name]
    policy = field.policy or default_policy
    return {
        "key": field.key,
        "member": field.name,
        "type": field.type_name,
        "value_kind": kind,
        "value_format": value_format,
        "policy": policy,
    }


def build_owner_paths(structs: list[StructDesc]) -> dict[str, list[str]]:
    by_name = {item.name: item for item in structs}
    root = next(item for item in structs if item.kind == "root")
    paths: dict[str, list[str]] = {}

    def visit(owner_type: str, path: list[str]) -> None:
        owner = by_name[owner_type]
        for item in owner.fields:
            if item.runtime_only:
                continue
            vector_item = vector_item_type(item.type_name)
            if vector_item is not None and vector_item != "std::string":
                target = by_name[vector_item]
                if target.kind == "dynamic":
                    paths.setdefault(vector_item, path + [item.name])
                continue
            target = by_name[item.type_name]
            if target.kind in {"static", "custom"}:
                paths.setdefault(item.type_name, path + [item.name])
            elif target.kind == "container":
                visit(item.type_name, path + [item.name])

    visit(root.name, [])
    return paths


def offset_expr(path: Iterable[tuple[str, str]]) -> str:
    terms = [f"offsetof({owner_type}, {member})" for owner_type, member in path]
    if not terms:
        return "0"
    return " + ".join(terms)


def root_offset_expr(paths: dict[str, list[str]], root_name: str, owner_type: str, field_name: str) -> str:
    members = paths[owner_type] + [field_name]
    owners: list[tuple[str, str]] = []
    current_type = root_name
    by_name = ROOT_CONTEXT_BY_NAME
    for member in members:
        owners.append((current_type, member))
        current = by_name[current_type]
        field = next(item for item in current.fields if item.name == member)
        vector_item = vector_item_type(field.type_name)
        current_type = vector_item or field.type_name
    return offset_expr(owners)


ROOT_CONTEXT_BY_NAME: dict[str, StructDesc] = {}


def cpp_string(value: str) -> str:
    return json.dumps(value)


def generate_runtime_field_array(struct: StructDesc) -> str:
    array_name = f"k{struct.name}Fields"
    lines = [f"constexpr RuntimeConfigFieldDescriptor {array_name}[] = {{"]
    for item in struct.config_fields():
        meta = field_metadata(item)
        lines.append(
            "    {"
            f"{cpp_string(meta['key'])}, "
            f"static_cast<std::uint32_t>(offsetof({struct.name}, {item.name})), "
            f"static_cast<std::uint8_t>({len(meta['key'])}), "
            f"RuntimeConfigFieldValueKind::{meta['value_kind']}, "
            f"RuntimeConfigFieldPolicy::{POLICY_CODE[meta['policy']]}"
            "},"
        )
    lines.append("};")
    return "\n".join(lines)


def access_expr(root_var: str, members: list[str]) -> str:
    if not members:
        return root_var
    return root_var + "." + ".".join(members)


def member_path_offset_expr(structs: list[StructDesc], root_name: str, members: list[str]) -> str:
    owners: list[tuple[str, str]] = []
    current_type = root_name
    by_name = {item.name: item for item in structs}
    for member in members:
        owners.append((current_type, member))
        current = by_name[current_type]
        field = next(item for item in current.fields if item.name == member)
        vector_item = vector_item_type(field.type_name)
        current_type = vector_item or field.type_name
    return offset_expr(owners)


def flattened_sections(structs: list[StructDesc]) -> list[tuple[StructDesc, list[str]]]:
    by_name = {item.name: item for item in structs}
    root = next(item for item in structs if item.kind == "root")
    sections: list[tuple[StructDesc, list[str]]] = []

    def visit(owner_type: str, path: list[str]) -> None:
        owner = by_name[owner_type]
        for item in owner.fields:
            if item.runtime_only:
                continue
            vector_item = vector_item_type(item.type_name)
            if vector_item is not None and vector_item != "std::string":
                target = by_name[vector_item]
                if target.kind == "dynamic":
                    sections.append((target, path + [item.name]))
                continue
            target = by_name[item.type_name]
            if target.kind == "container":
                visit(item.type_name, path + [item.name])
            elif target.kind in {"static", "custom"}:
                sections.append((target, path + [item.name]))

    visit(root.name, [])
    return sections


def field_array_name(struct: StructDesc) -> str:
    return f"k{struct.name}Fields"


def field_array_count_expr(struct: StructDesc) -> str:
    name = field_array_name(struct)
    return f"static_cast<std::uint32_t>(sizeof({name}) / sizeof({name}[0]))"


def runtime_section_codec(struct: StructDesc) -> str:
    if struct.kind == "custom":
        return (struct.codec or "").removesuffix("SectionCodec")
    return "Structured"


def dynamic_callback_prefix(struct: StructDesc) -> str:
    return struct.name.removesuffix("Config")


def generate_dynamic_callbacks(structs: list[StructDesc], root_name: str, struct: StructDesc, members: list[str]) -> str:
    key_member = struct.key_member or ""
    prefix = dynamic_callback_prefix(struct)
    mutable_items = access_expr("config", members)
    const_items = access_expr("config", members)
    return "\n".join(
        [
            f"void* Ensure{prefix}ConfigItem(AppConfig& config, std::string_view key) {{",
            f"    auto& items = {mutable_items};",
            "    for (auto& item : items) {",
            f"        if (item.{key_member} == key) {{",
            "            return &item;",
            "        }",
            "    }",
            f"    items.push_back({struct.name}{{}});",
            f"    items.back().{key_member} = std::string(key);",
            "    return &items.back();",
            "}",
            "",
            f"const void* Find{prefix}ConfigItem(const AppConfig& config, std::string_view key) {{",
            f"    const auto& items = {const_items};",
            "    for (const auto& item : items) {",
            f"        if (item.{key_member} == key) {{",
            "            return &item;",
            "        }",
            "    }",
            "    return nullptr;",
            "}",
            "",
            f"void ForEach{prefix}ConfigItem(",
            "    const AppConfig& config, void* context, RuntimeConfigDynamicItemVisitor visitor) {",
            f"    const auto& items = {const_items};",
            "    for (const auto& item : items) {",
            f"        visitor(context, item.{key_member}, &item);",
            "    }",
            "}",
        ]
    )


def generate_runtime_section_table(structs: list[StructDesc]) -> str:
    root = next(item for item in structs if item.kind == "root")
    section_items = flattened_sections(structs)
    lines: list[str] = []
    generated_dynamic: set[str] = set()
    for struct, members in section_items:
        if struct.kind != "dynamic" or struct.name in generated_dynamic:
            continue
        lines.append(generate_dynamic_callbacks(structs, root.name, struct, members))
        lines.append("")
        generated_dynamic.add(struct.name)

    lines.append("constexpr RuntimeConfigSectionDescriptor kRuntimeConfigSections[] = {")
    for struct, members in section_items:
        offset = member_path_offset_expr(structs, root.name, members)
        if struct.kind == "dynamic":
            prefix = dynamic_callback_prefix(struct)
            section_name = struct.prefix or ""
            fields_name = field_array_name(struct)
            field_count = field_array_count_expr(struct)
            callbacks = (
                f"RuntimeConfigDynamicSectionCallbacks{{Ensure{prefix}ConfigItem, "
                f"Find{prefix}ConfigItem, ForEach{prefix}ConfigItem}}"
            )
            item_size = f"static_cast<std::uint32_t>(sizeof({struct.name}))"
            key_offset = f"static_cast<std::uint32_t>(offsetof({struct.name}, {struct.key_member}))"
            kind = "Dynamic"
        else:
            section_name = struct.section or ""
            if struct.kind == "custom":
                fields_name = "nullptr"
                field_count = "0"
                callbacks = "{}"
                kind = "Custom"
            else:
                fields_name = field_array_name(struct)
                field_count = field_array_count_expr(struct)
                callbacks = "{}"
                kind = "Static"
            item_size = "0"
            key_offset = "0"
        lines.append(
            "    {"
            f"{cpp_string(section_name)}, "
            f"static_cast<std::uint32_t>({offset}), "
            f"{key_offset}, "
            f"{item_size}, "
            f"static_cast<std::uint8_t>({len(section_name)}), "
            f"RuntimeConfigSectionKind::{kind}, "
            f"RuntimeConfigSectionCodec::{runtime_section_codec(struct)}, "
            f"{fields_name}, "
            f"{field_count}, "
            f"{callbacks}"
            "},"
        )
    lines.append("};")
    return "\n".join(lines)


def generate_layout_edit_table(
    structs: list[StructDesc], paths: dict[str, list[str]], active_parameters: list[tuple[str, str, str]]
) -> str:
    by_name = {item.name: item for item in structs}
    root = next(item for item in structs if item.kind == "root")
    lines = ["constexpr LayoutEditConfigFieldMetadata kLayoutEditConfigFieldMetadata[] = {"]
    for enum_name, owner_type, field_name in active_parameters:
        owner = by_name.get(owner_type)
        if owner is None:
            raise ConfigMetaError(f"layout edit parameter '{enum_name}' references unknown owner '{owner_type}'")
        field = next((item for item in owner.config_fields() if item.name == field_name), None)
        if field is None:
            raise ConfigMetaError(f"layout edit parameter '{enum_name}' references unknown field '{owner_type}::{field_name}'")
        if owner.kind != "static":
            raise ConfigMetaError(f"layout edit parameter '{enum_name}' must reference a static section field")
        if owner_type not in paths:
            raise ConfigMetaError(f"layout edit parameter '{enum_name}' owner '{owner_type}' is not reachable from AppConfig")
        meta = field_metadata(field)
        root_expr = root_offset_expr(paths, root.name, owner_type, field_name)
        lines.append(
            "    {"
            f"{cpp_string(owner.section or '')}, "
            f"{cpp_string(meta['key'])}, "
            f"configschema::ValueFormat::{meta['value_format']}, "
            f"RuntimeConfigFieldValueKind::{meta['value_kind']}, "
            f"RuntimeConfigFieldPolicy::{POLICY_CODE[meta['policy']]}, "
            f"static_cast<std::uint32_t>({root_expr})"
            "},"
        )
    lines.append("};")
    return "\n".join(lines)


def generate_cpp(structs: list[StructDesc], paths: dict[str, list[str]]) -> str:
    field_structs = [item for item in structs if item.kind in {"static", "dynamic"}]
    lines = [
        '#include "config/config_runtime_fields.h"',
        "",
        "#include <cstddef>",
        "#include <span>",
        "",
        "namespace {",
        "",
    ]
    for struct in field_structs:
        lines.append(generate_runtime_field_array(struct))
        lines.append("")
    lines.append(generate_runtime_section_table(structs))
    lines.append("")
    lines.extend(["}  // namespace", ""])
    lines.extend(
        [
            "std::span<const RuntimeConfigSectionDescriptor> RuntimeConfigSectionDescriptors() {",
            "    return kRuntimeConfigSections;",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def generate_layout_edit_cpp(
    structs: list[StructDesc], paths: dict[str, list[str]], active_parameters: list[tuple[str, str, str]]
) -> str:
    lines = [
        '#include "layout_model/layout_edit_parameter_metadata.h"',
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <span>",
        "",
        "namespace {",
        "",
        generate_layout_edit_table(structs, paths, active_parameters),
        "",
        "static_assert(sizeof(kLayoutEditConfigFieldMetadata) / sizeof(kLayoutEditConfigFieldMetadata[0]) ==",
        "    static_cast<std::size_t>(LayoutEditParameter::Count));",
        "",
        "}  // namespace",
        "",
        "std::span<const LayoutEditConfigFieldMetadata> LayoutEditConfigFieldMetadataDescriptors() {",
        "    return kLayoutEditConfigFieldMetadata;",
        "}",
        "",
    ]
    return "\n".join(lines)


def build_manifest(
    structs: list[StructDesc], paths: dict[str, list[str]], active_parameters: list[tuple[str, str, str]]
) -> dict[str, object]:
    sections: list[dict[str, object]] = []
    dynamic_sections: list[dict[str, object]] = []
    custom_sections: list[dict[str, object]] = []
    editable_fields: list[dict[str, object]] = []

    for struct in structs:
        if struct.kind == "static":
            fields = [field_metadata(item) for item in struct.config_fields()]
            sections.append({"name": f"[{struct.section}]", "type": struct.name, "fields": fields})
            for item in fields:
                editable_fields.append(
                    {"scope": "root", "section": f"[{struct.section}]", "type": struct.name, **item}
                )
        elif struct.kind == "dynamic":
            fields = [field_metadata(item) for item in struct.config_fields()]
            dynamic_sections.append(
                {
                    "pattern": struct.pattern,
                    "prefix": struct.prefix,
                    "type": struct.name,
                    "key_member": struct.key_member,
                    "fields": fields,
                }
            )
            scope = {
                "ThemeConfig": "ActiveTheme",
                "LayoutSectionConfig": "ActiveNamedLayout",
                "LayoutCardConfig": "CardById",
            }.get(struct.name, "ItemKey")
            for item in fields:
                editable_fields.append(
                    {"scope": scope, "section": struct.pattern, "type": struct.name, "key_member": struct.key_member, **item}
                )
        elif struct.kind == "custom":
            custom_sections.append(
                {"name": f"[{struct.section}]", "type": struct.name, "codec_type": struct.codec}
            )

    layout_parameters = []
    by_name = {item.name: item for item in structs}
    root = next(item for item in structs if item.kind == "root")
    for enum_name, owner_type, field_name in active_parameters:
        owner = by_name[owner_type]
        source_field = next(item for item in owner.config_fields() if item.name == field_name)
        meta = field_metadata(source_field)
        layout_parameters.append(
            {
                "id": enum_name,
                "section": f"[{owner.section}]",
                "parameter": meta["key"],
                "type": owner_type,
                "member": field_name,
                "root_offset_expr": root_offset_expr(paths, root.name, owner_type, field_name),
                "value_kind": meta["value_kind"],
                "value_format": meta["value_format"],
                "policy": meta["policy"],
            }
        )

    return {
        "sections": sections,
        "dynamic_sections": dynamic_sections,
        "custom_sections": custom_sections,
        "editable_fields": editable_fields,
        "layout_edit_parameters": layout_parameters,
    }


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", required=True, type=Path)
    parser.add_argument("--primitives", required=True, type=Path)
    parser.add_argument("--layout-edit-parameters", required=True, type=Path)
    parser.add_argument("--resource-config", required=True, type=Path)
    parser.add_argument("--output-cpp", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-layout-edit-cpp", required=True, type=Path)
    args = parser.parse_args()

    try:
        structs = parse_descriptor(args.descriptor) + parse_primitive_annotations(args.primitives)
        resources = parse_resource_config(args.resource_config)
        validate_descriptor(structs, resources)
        global ROOT_CONTEXT_BY_NAME
        ROOT_CONTEXT_BY_NAME = {item.name: item for item in structs}
        paths = build_owner_paths(structs)
        layout_edit_parameters = resolve_layout_edit_parameters(args.layout_edit_parameters, structs)
        cpp = generate_cpp(structs, paths)
        layout_edit_cpp = generate_layout_edit_cpp(structs, paths, layout_edit_parameters)
        manifest = json.dumps(build_manifest(structs, paths, layout_edit_parameters), indent=2) + "\n"
        write_if_changed(args.output_cpp, cpp)
        write_if_changed(args.output_layout_edit_cpp, layout_edit_cpp)
        write_if_changed(args.output_json, manifest)
    except ConfigMetaError as error:
        raise SystemExit(f"config_meta_gen: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
