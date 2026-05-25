#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def read_utf8_text(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise SystemExit(f"{path} must be UTF-8 without a BOM")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit(f"{path} is not valid UTF-8: {exc}") from exc


def write_if_changed(path: Path, text: str) -> None:
    data = text.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def extract_ini_section(text: str, section: str, source: Path) -> str:
    selected: list[str] = []
    in_section = False
    found = False
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section_name = stripped[1:-1].strip()
            if in_section and section_name != section:
                break
            in_section = section_name == section
            found = found or in_section
        if in_section:
            selected.append(line)

    if not found:
        raise SystemExit(f"{source}: section [{section}] was not found")
    section_text = "".join(selected)
    if section_text and not section_text.endswith("\n"):
        section_text += "\n"
    return section_text


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract one INI section into a generated resource script.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--section", required=True)
    parser.add_argument("--output-ini", required=True, type=Path)
    parser.add_argument("--output-rc", required=True, type=Path)
    parser.add_argument("--resource-id", required=True)
    args = parser.parse_args()

    section_text = extract_ini_section(read_utf8_text(args.input), args.section, args.input)
    write_if_changed(args.output_ini, section_text)

    rc_path = args.output_ini.as_posix()
    rc_text = f'#include "resource.h"\n\n{args.resource_id} RCDATA "{rc_path}"\n'
    write_if_changed(args.output_rc, rc_text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
