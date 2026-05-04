#!/usr/bin/env python3
from __future__ import annotations

import argparse
import binascii
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_chunk(chunk_type: bytes, chunk_data: bytes) -> bytes:
    return (
        struct.pack(">I", len(chunk_data))
        + chunk_type
        + chunk_data
        + struct.pack(">I", binascii.crc32(chunk_type + chunk_data) & 0xFFFFFFFF)
    )


def optimize_png(data: bytes) -> bytes:
    if not data.startswith(PNG_SIGNATURE):
        return data
    chunks: list[tuple[bytes, bytes]] = []
    idat = bytearray()
    offset = len(PNG_SIGNATURE)
    while offset + 8 <= len(data):
        chunk_size = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_start = offset + 8
        chunk_end = chunk_start + chunk_size
        if chunk_end + 4 > len(data):
            return data
        chunk_data = data[chunk_start:chunk_end]
        offset = chunk_end + 4
        if chunk_type == b"IDAT":
            idat.extend(chunk_data)
        else:
            chunks.append((chunk_type, chunk_data))
        if chunk_type == b"IEND":
            break
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return data
    compressed = zlib.compress(raw, 9)
    output = bytearray(PNG_SIGNATURE)
    wrote_idat = False
    for chunk_type, chunk_data in chunks:
        if chunk_type == b"IEND" and not wrote_idat:
            output.extend(png_chunk(b"IDAT", compressed))
            wrote_idat = True
        output.extend(png_chunk(chunk_type, chunk_data))
    optimized = bytes(output)
    return optimized if len(optimized) < len(data) else data


def optimize_ico(data: bytes) -> bytes:
    if len(data) < 6:
        return data
    reserved, icon_type, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or icon_type != 1 or len(data) < 6 + (16 * count):
        return data
    entries = []
    for index in range(count):
        entry_offset = 6 + (16 * index)
        width, height, colors, reserved_byte, planes, bit_count, size, image_offset = struct.unpack_from(
            "<BBBBHHII", data, entry_offset
        )
        if image_offset + size > len(data):
            return data
        image = optimize_png(data[image_offset : image_offset + size])
        entries.append((width, height, colors, reserved_byte, planes, bit_count, image))

    output = bytearray(struct.pack("<HHH", reserved, icon_type, count))
    image_offset = 6 + (16 * count)
    for width, height, colors, reserved_byte, planes, bit_count, image in entries:
        output.extend(
            struct.pack("<BBBBHHII", width, height, colors, reserved_byte, planes, bit_count, len(image), image_offset)
        )
        image_offset += len(image)
    for *_, image in entries:
        output.extend(image)
    optimized = bytes(output)
    return optimized if len(optimized) < len(data) else data


def optimize_file(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if path.suffix.lower() == ".ico":
        optimized = optimize_ico(data)
    else:
        optimized = optimize_png(data)
    if optimized != data:
        path.write_bytes(optimized)
    return len(data), len(optimized)


def main() -> int:
    parser = argparse.ArgumentParser(description="Losslessly recompress PNG and PNG-backed ICO resources.")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()

    for path in args.paths:
        before, after = optimize_file(path)
        print(f"{path}: {before} -> {after}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
