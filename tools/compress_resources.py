#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = b"CDLZ"
MAX_OFFSET = 4095
MIN_MATCH = 3
MAX_MATCH = 18


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Compress embedded text resources for CaseDash.")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--output-rc", required=True, type=Path)
    parser.add_argument("--resource-root", required=True, type=Path)
    args = parser.parse_args()

    config = (args.resource_root / "config.ini").read_bytes()
    localization = (args.resource_root / "localization.ini").read_bytes()
    atlas = struct.pack("<I", len(config)) + config + localization
    compressed = MAGIC + struct.pack("<I", len(atlas)) + lzss_compress(atlas)
    compressed_path = args.output_dir / "text_atlas.cdlz"
    write_if_changed(compressed_path, compressed)

    rc_path = compressed_path.as_posix()
    rc_lines = ['#include "resource.h"', "", f'IDR_TEXT_RESOURCE_ATLAS RCDATA "{rc_path}"']
    rc_lines.append("")
    write_if_changed(args.output_rc, "\n".join(rc_lines).encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
