#!/usr/bin/env python3
from __future__ import annotations

import argparse
import binascii
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
STRIPPED_ANCILLARY_CHUNKS = {b"gAMA", b"pHYs", b"sRGB"}
PNG_COLOR_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
ZLIB_STRATEGIES = (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED)


def png_chunk(chunk_type: bytes, chunk_data: bytes) -> bytes:
    return (
        struct.pack(">I", len(chunk_data))
        + chunk_type
        + chunk_data
        + struct.pack(">I", binascii.crc32(chunk_type + chunk_data) & 0xFFFFFFFF)
    )


def compressed_zlib(data: bytes) -> bytes:
    best = zlib.compress(data, 9)
    for strategy in ZLIB_STRATEGIES:
        compressor = zlib.compressobj(9, zlib.DEFLATED, zlib.MAX_WBITS, zlib.DEF_MEM_LEVEL, strategy)
        candidate = compressor.compress(data) + compressor.flush()
        if len(candidate) < len(best):
            best = candidate
    return best


def png_scanline_geometry(width: int, bit_depth: int, color_type: int) -> tuple[int, int] | None:
    channels = PNG_COLOR_CHANNELS.get(color_type)
    if channels is None:
        return None
    bits_per_pixel = bit_depth * channels
    return max(1, (bits_per_pixel + 7) // 8), (width * bits_per_pixel + 7) // 8


def paeth_predictor(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def unfilter_png_rows(raw: bytes, width: int, height: int, bit_depth: int, color_type: int) -> list[bytes] | None:
    geometry = png_scanline_geometry(width, bit_depth, color_type)
    if geometry is None:
        return None
    bytes_per_pixel, stride = geometry
    if len(raw) != height * (stride + 1):
        return None

    rows: list[bytes] = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        if filter_type > 4:
            return None
        encoded = raw[offset : offset + stride]
        offset += stride
        decoded = bytearray(stride)
        for index, value in enumerate(encoded):
            left = decoded[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            above = previous[index]
            upper_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            if filter_type == 0:
                decoded[index] = value
            elif filter_type == 1:
                decoded[index] = (value + left) & 0xFF
            elif filter_type == 2:
                decoded[index] = (value + above) & 0xFF
            elif filter_type == 3:
                decoded[index] = (value + ((left + above) // 2)) & 0xFF
            else:
                decoded[index] = (value + paeth_predictor(left, above, upper_left)) & 0xFF
        rows.append(bytes(decoded))
        previous = decoded
    return rows


def filter_png_row(row: bytes, previous: bytes | None, bytes_per_pixel: int, filter_type: int) -> bytes:
    output = bytearray([filter_type])
    for index, value in enumerate(row):
        left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
        above = previous[index] if previous is not None else 0
        upper_left = previous[index - bytes_per_pixel] if previous is not None and index >= bytes_per_pixel else 0
        if filter_type == 0:
            encoded = value
        elif filter_type == 1:
            encoded = value - left
        elif filter_type == 2:
            encoded = value - above
        elif filter_type == 3:
            encoded = value - ((left + above) // 2)
        else:
            encoded = value - paeth_predictor(left, above, upper_left)
        output.append(encoded & 0xFF)
    return bytes(output)


def refilter_png(raw: bytes, ihdr: bytes) -> bytes:
    width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(">IIBBBBB", ihdr)
    if compression != 0 or filter_method != 0 or interlace != 0:
        return raw
    geometry = png_scanline_geometry(width, bit_depth, color_type)
    rows = unfilter_png_rows(raw, width, height, bit_depth, color_type)
    if geometry is None or rows is None:
        return raw

    bytes_per_pixel, _ = geometry
    candidates = [raw]
    for filter_type in range(5):
        previous = None
        filtered = bytearray()
        for row in rows:
            filtered.extend(filter_png_row(row, previous, bytes_per_pixel, filter_type))
            previous = row
        candidates.append(bytes(filtered))
    return min(candidates, key=lambda candidate: len(compressed_zlib(candidate)))


def indexed_png_candidate(ihdr: bytes, raw: bytes) -> bytes | None:
    width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(">IIBBBBB", ihdr)
    if bit_depth != 8 or color_type != 6 or compression != 0 or filter_method != 0 or interlace != 0:
        return None

    rows = unfilter_png_rows(raw, width, height, bit_depth, color_type)
    if rows is None:
        return None

    palette: list[bytes] = []
    palette_indexes: dict[bytes, int] = {}
    indexed_rows: list[bytes] = []
    for row in rows:
        indexed_row = bytearray()
        for offset in range(0, len(row), 4):
            pixel = row[offset : offset + 4]
            if pixel not in palette_indexes:
                if len(palette) >= 256:
                    return None
                palette_indexes[pixel] = len(palette)
                palette.append(pixel)
            indexed_row.append(palette_indexes[pixel])
        indexed_rows.append(bytes(indexed_row))

    indexed_raw = bytearray()
    for row in indexed_rows:
        indexed_raw.append(0)
        indexed_raw.extend(row)

    indexed_ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, compression, filter_method, interlace)
    palette_data = b"".join(pixel[:3] for pixel in palette)
    alpha_values = bytearray(pixel[3] for pixel in palette)
    while alpha_values and alpha_values[-1] == 0xFF:
        alpha_values.pop()

    output = bytearray(PNG_SIGNATURE)
    output.extend(png_chunk(b"IHDR", indexed_ihdr))
    output.extend(png_chunk(b"PLTE", palette_data))
    if alpha_values:
        output.extend(png_chunk(b"tRNS", bytes(alpha_values)))
    output.extend(png_chunk(b"IDAT", compressed_zlib(bytes(indexed_raw))))
    output.extend(png_chunk(b"IEND", b""))
    return bytes(output)


def optimize_png(data: bytes) -> bytes:
    if not data.startswith(PNG_SIGNATURE):
        return data
    chunks: list[tuple[bytes, bytes]] = []
    idat = bytearray()
    ihdr = b""
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
        if chunk_type in STRIPPED_ANCILLARY_CHUNKS:
            pass
        elif chunk_type == b"IHDR":
            ihdr = chunk_data
            chunks.append((chunk_type, chunk_data))
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        else:
            chunks.append((chunk_type, chunk_data))
        if chunk_type == b"IEND":
            break
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return data
    filtered_raw = refilter_png(raw, ihdr) if ihdr else raw
    compressed = compressed_zlib(filtered_raw)
    output = bytearray(PNG_SIGNATURE)
    wrote_idat = False
    for chunk_type, chunk_data in chunks:
        if chunk_type == b"IEND" and not wrote_idat:
            output.extend(png_chunk(b"IDAT", compressed))
            wrote_idat = True
        output.extend(png_chunk(chunk_type, chunk_data))
    optimized = bytes(output)
    if ihdr and all(chunk_type in {b"IHDR", b"IEND"} for chunk_type, _ in chunks):
        indexed = indexed_png_candidate(ihdr, raw)
        if indexed is not None and len(indexed) < len(optimized):
            optimized = indexed
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
    parser = argparse.ArgumentParser(description="Recompress PNG/ICO resources and strip nonessential PNG metadata.")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()

    for path in args.paths:
        before, after = optimize_file(path)
        print(f"{path}: {before} -> {after}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
