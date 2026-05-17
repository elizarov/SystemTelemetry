#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = b"CDLZ"
MAX_OFFSET = 4095
MIN_MATCH = 3
MAX_MATCH = 18
RESOURCE_STRING_HASH_SEED = 2166136261
RESOURCE_STRING_HASH_PRIME = 16777619


def lzss_compress(data: bytes) -> bytes:
    output = bytearray()
    index = 0
    while index < len(data):
        flags_offset = len(output)
        output.append(0)
        flags = 0
        for bit in range(8):
            if index >= len(data):
                break
            best_offset = 0
            best_length = 0
            search_start = max(0, index - MAX_OFFSET)
            max_length = min(MAX_MATCH, len(data) - index)
            for candidate in range(index - 1, search_start - 1, -1):
                length = 0
                while length < max_length and data[candidate + length] == data[index + length]:
                    length += 1
                if length >= MIN_MATCH and length > best_length:
                    best_offset = index - candidate
                    best_length = length
                    if length == max_length:
                        break
            if best_length >= MIN_MATCH:
                flags |= 1 << bit
                token = ((best_offset - 1) << 4) | (best_length - MIN_MATCH)
                output.extend(struct.pack("<H", token))
                index += best_length
            else:
                output.append(data[index])
                index += 1
        output[flags_offset] = flags
    return bytes(output)


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


def collect_resource_strings(source_root: Path) -> list[str]:
    strings: set[str] = set()
    for path in sorted((source_root / "src").rglob("*.cpp")):
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
            strings.add(value)
            index = close + 1
    return sorted(strings)


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
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace resource_strings_detail {",
        "",
        f"constexpr std::uint32_t ResourceStringHashSeed = 0x{hash_seed:08X}u;",
        f"constexpr std::uint32_t ResourceStringHashPrime = {RESOURCE_STRING_HASH_PRIME}u;",
        "",
        "constexpr std::uint32_t ResourceStringHash(const char* text, std::size_t length) {",
        "    std::uint32_t hash = ResourceStringHashSeed;",
        "    for (std::size_t index = 0; index < length; ++index) {",
        "        hash ^= static_cast<std::uint8_t>(text[index]);",
        "        hash *= ResourceStringHashPrime;",
        "    }",
        "    return hash;",
        "}",
        "",
        "template <std::size_t Size>",
        "consteval ResourceStringId MakeResourceStringId(const char (&text)[Size]) {",
        "    return static_cast<ResourceStringId>(ResourceStringHash(text, Size - 1));",
        "}",
        "",
        "}  // namespace resource_strings_detail",
        "",
        "#define RES_STR(text) (::resource_strings_detail::MakeResourceStringId(text))",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compress embedded text resources for CaseDash.")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--output-rc", required=True, type=Path)
    parser.add_argument("--resource-root", required=True, type=Path)
    parser.add_argument("--resource-string-header", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    config = read_utf8_bytes(args.resource_root / "config.ini")
    localization = read_utf8_bytes(args.resource_root / "localization.ini")
    resource_strings = collect_resource_strings(args.source_root)
    resource_string_text = "\n".join(resource_strings)
    if resource_strings:
        resource_string_text += "\n"
    resource_string_bytes = resource_string_text.encode("utf-8")
    resource_string_text_path = args.output_dir / "resource_strings.txt"
    write_if_changed(resource_string_text_path, resource_string_bytes)
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
