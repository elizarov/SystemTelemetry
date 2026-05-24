#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from collections import defaultdict
from pathlib import Path


MAGIC = b"CDLZ"
MAX_OFFSET = 4095
MIN_MATCH = 3
EXTENDED_MATCH_LENGTH = MIN_MATCH + 15
MAX_MATCH = EXTENDED_MATCH_LENGTH + 255
RESOURCE_STRING_HASH_SEED = 2166136261
RESOURCE_STRING_HASH_PRIME = 16777619


def lzss_compress(data: bytes) -> bytes:
    best_lengths, best_offsets = find_lzss_matches(data)
    costs = [[0] * 8 for _ in range(len(data) + 1)]
    choices = [[1] * 8 for _ in range(len(data))]

    for index in range(len(data) - 1, -1, -1):
        for flag_bit in range(8):
            next_flag_bit = (flag_bit + 1) % 8
            flag_cost = 1 if flag_bit == 0 else 0
            best_cost = flag_cost + 1 + costs[index + 1][next_flag_bit]
            best_length = 1
            for length in range(MIN_MATCH, best_lengths[index] + 1):
                match_cost = 3 if length >= EXTENDED_MATCH_LENGTH else 2
                cost = flag_cost + match_cost + costs[index + length][next_flag_bit]
                if cost < best_cost or (cost == best_cost and length > best_length):
                    best_cost = cost
                    best_length = length
            costs[index][flag_bit] = best_cost
            choices[index][flag_bit] = best_length

    output = bytearray()
    index = 0
    flag_bit = 0
    flags_offset = 0
    flags = 0
    while index < len(data):
        if flag_bit == 0:
            flags_offset = len(output)
            output.append(0)
            flags = 0

        length = choices[index][flag_bit]
        if length >= MIN_MATCH:
            flags |= 1 << flag_bit
            length_code = length - MIN_MATCH
            if length >= EXTENDED_MATCH_LENGTH:
                length_code = EXTENDED_MATCH_LENGTH - MIN_MATCH
            token = ((best_offsets[index] - 1) << 4) | length_code
            output.extend(struct.pack("<H", token))
            if length >= EXTENDED_MATCH_LENGTH:
                output.append(length - EXTENDED_MATCH_LENGTH)
            index += length
        else:
            output.append(data[index])
            index += 1

        flag_bit = (flag_bit + 1) % 8
        if flag_bit == 0 or index >= len(data):
            output[flags_offset] = flags
    return bytes(output)


def find_lzss_matches(data: bytes) -> tuple[list[int], list[int]]:
    best_lengths = [0] * len(data)
    best_offsets = [0] * len(data)
    positions_by_prefix: defaultdict[bytes, list[int]] = defaultdict(list)

    for index in range(len(data)):
        if index + MIN_MATCH <= len(data):
            prefix = data[index : index + MIN_MATCH]
            positions = positions_by_prefix[prefix]
            search_start = index - MAX_OFFSET
            stale_count = 0
            while stale_count < len(positions) and positions[stale_count] < search_start:
                stale_count += 1
            if stale_count:
                del positions[:stale_count]

            max_length = min(MAX_MATCH, len(data) - index)
            for candidate in reversed(positions):
                length = MIN_MATCH
                while length < max_length and data[candidate + length] == data[index + length]:
                    length += 1
                if length > best_lengths[index]:
                    best_offsets[index] = index - candidate
                    best_lengths[index] = length
                    if length == max_length:
                        break

        if index + MIN_MATCH <= len(data):
            positions_by_prefix[data[index : index + MIN_MATCH]].append(index)

    return best_lengths, best_offsets


def write_if_changed(path: Path, data: bytes) -> None:
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def read_utf8_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise SystemExit(f"{path} must be UTF-8 without a BOM")
    try:
        data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit(f"{path} is not valid UTF-8: {exc}") from exc
    return data


def remove_ini_sections(text: str, excluded_sections: set[str]) -> str:
    output: list[str] = []
    skip_section = False
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section_name = stripped[1:-1].strip()
            skip_section = section_name in excluded_sections
        if not skip_section:
            output.append(line)
    return "".join(output)


def is_identifier_char(ch: str) -> bool:
    return ch == "_" or ch.isalnum()


def skip_whitespace(text: str, index: int) -> int:
    while index < len(text) and text[index].isspace():
        index += 1
    return index


def decode_c_string_literal(source: str, path: Path) -> str:
    if len(source) < 2 or source[0] != '"' or source[-1] != '"':
        raise SystemExit(f"{path}: RES_STR only supports ordinary narrow string literals")

    escapes = {
        '"': '"',
        "'": "'",
        "?": "?",
        "\\": "\\",
        "a": "\a",
        "b": "\b",
        "f": "\f",
        "n": "\n",
        "r": "\r",
        "t": "\t",
        "v": "\v",
    }
    output = []
    index = 1
    while index < len(source) - 1:
        ch = source[index]
        if ch != "\\":
            output.append(ch)
            index += 1
            continue

        index += 1
        if index >= len(source) - 1:
            raise SystemExit(f"{path}: RES_STR literal ends with a dangling escape")
        escape = source[index]
        index += 1
        if escape in escapes:
            output.append(escapes[escape])
            continue
        if escape in "01234567":
            digits = escape
            while index < len(source) - 1 and len(digits) < 3 and source[index] in "01234567":
                digits += source[index]
                index += 1
            value = int(digits, 8)
            if value > 0x7F:
                raise SystemExit(f"{path}: RES_STR octal escapes above ASCII are not supported")
            output.append(chr(value))
            continue
        if escape == "x":
            start = index
            while index < len(source) - 1 and source[index] in "0123456789abcdefABCDEF":
                index += 1
            if start == index:
                raise SystemExit(f"{path}: RES_STR hex escape is missing digits")
            value = int(source[start:index], 16)
            if value > 0x7F:
                raise SystemExit(f"{path}: RES_STR hex escapes above ASCII are not supported")
            output.append(chr(value))
            continue
        raise SystemExit(f"{path}: RES_STR escape \\{escape} is not supported")

    return "".join(output)


def parse_c_string_literal_sequence(text: str, index: int, path: Path) -> tuple[str, int]:
    pieces = []
    index = skip_whitespace(text, index)
    if index >= len(text) or text[index] != '"':
        raise SystemExit(f"{path}: RES_STR expects a string literal")

    while index < len(text) and text[index] == '"':
        start = index
        index += 1
        while index < len(text):
            if text[index] == "\\":
                index += 2
                continue
            if text[index] == '"':
                index += 1
                break
            index += 1
        else:
            raise SystemExit(f"{path}: unterminated RES_STR string literal")

        pieces.append(decode_c_string_literal(text[start:index], path))
        index = skip_whitespace(text, index)

    return "".join(pieces), index


def collect_resource_strings(source_root: Path, excluded_prefixes: list[str]) -> list[str]:
    strings: dict[str, None] = {}
    for path in sorted((source_root / "src").rglob("*.cpp")):
        relative_path = path.relative_to(source_root).as_posix()
        if any(relative_path.startswith(prefix) for prefix in excluded_prefixes):
            continue
        text = path.read_text(encoding="utf-8")
        index = 0
        while True:
            found = text.find("RES_STR", index)
            if found < 0:
                break
            before = text[found - 1] if found > 0 else ""
            after = text[found + len("RES_STR")] if found + len("RES_STR") < len(text) else ""
            if (before and is_identifier_char(before)) or (after and is_identifier_char(after)):
                index = found + len("RES_STR")
                continue
            call = skip_whitespace(text, found + len("RES_STR"))
            if call >= len(text) or text[call] != "(":
                index = found + len("RES_STR")
                continue
            value, end = parse_c_string_literal_sequence(text, call + 1, path)
            close = skip_whitespace(text, end)
            if close >= len(text) or text[close] != ")":
                raise SystemExit(f"{path}: RES_STR only supports one string-literal argument")
            if "\n" in value or "\r" in value or "\0" in value:
                raise SystemExit(f"{path}: RES_STR values must stay single-line trace strings")
            strings.setdefault(value, None)
            index = close + 1
    return list(strings)


def resource_string_hash(text: str, seed: int) -> int:
    value = seed
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * RESOURCE_STRING_HASH_PRIME) & 0xFFFFFFFF
    return value


def find_resource_string_hash_seed(strings: list[str]) -> int:
    for attempt in range(4096):
        seed = (RESOURCE_STRING_HASH_SEED + attempt * 0x9E3779B1) & 0xFFFFFFFF
        hashes = {resource_string_hash(value, seed) for value in strings}
        if len(hashes) == len(strings):
            return seed
    raise SystemExit("RES_STR hash ids collided for every generated seed attempt")


def build_resource_string_header(strings: list[str]) -> str:
    hash_seed = find_resource_string_hash_seed(strings)
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace resource_strings_detail {",
        "",
        f"inline constexpr std::uint32_t ResourceStringHashSeed = 0x{hash_seed:08X}u;",
        "",
        "}  // namespace resource_strings_detail",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compress embedded text resources for CaseDash.")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--output-rc", required=True, type=Path)
    parser.add_argument("--resource-root", required=True, type=Path)
    parser.add_argument("--resource-string-header", type=Path)
    parser.add_argument("--exclude-config-section", action="append", default=[])
    parser.add_argument("--exclude-source-prefix", action="append", default=[])
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    config = read_utf8_bytes(args.resource_root / "config.ini")
    if args.exclude_config_section:
        config = remove_ini_sections(config.decode("utf-8"), set(args.exclude_config_section)).encode("utf-8")
    localization = read_utf8_bytes(args.resource_root / "localization.ini")
    excluded_prefixes = [prefix.replace("\\", "/").rstrip("/") + "/" for prefix in args.exclude_source_prefix]
    resource_strings = collect_resource_strings(args.source_root, excluded_prefixes)
    resource_string_text = "\n".join(resource_strings)
    if resource_strings:
        resource_string_text += "\n"
    resource_string_bytes = resource_string_text.encode("utf-8")
    resource_string_text_path = args.output_dir / "resource_strings.txt"
    write_if_changed(resource_string_text_path, resource_string_bytes)
    if args.resource_string_header is not None:
        write_if_changed(args.resource_string_header, build_resource_string_header(resource_strings).encode("utf-8"))

    atlas = struct.pack("<II", len(config), len(localization)) + config + localization + resource_string_bytes
    compressed = MAGIC + struct.pack("<I", len(atlas)) + lzss_compress(atlas)
    compressed_path = args.output_dir / "text_atlas.cdlz"
    write_if_changed(compressed_path, compressed)

    rc_path = compressed_path.as_posix()
    rc_lines = [
        '#include "resource.h"',
        "",
        f'IDR_TEXT_RESOURCE_ATLAS RCDATA "{rc_path}"',
    ]
    rc_lines.append("")
    write_if_changed(args.output_rc, "\n".join(rc_lines).encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
