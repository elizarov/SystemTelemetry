#!/usr/bin/env python3
"""Summarize an MSVC linker map for size-oriented follow-up work.

The MSVC map records symbol addresses but not exact symbol byte counts. This
tool estimates symbol contribution by taking the distance to the next symbol in
the same section and then aggregates those estimates by object and library.
Treat the output as a ranked investigation guide, not an exact replacement for
the linker.
"""

from __future__ import annotations

import argparse
import collections
import re
from dataclasses import dataclass
from pathlib import Path


SECTION_RE = re.compile(
    r"^\s+(?P<section>[0-9A-Fa-f]{4}):(?P<start>[0-9A-Fa-f]{8})\s+"
    r"(?P<length>[0-9A-Fa-f]+)H\s+(?P<name>\S+)\s+(?P<kind>\S+)"
)
SYMBOL_RE = re.compile(
    r"^\s+(?P<section>[0-9A-Fa-f]{4}):(?P<offset>[0-9A-Fa-f]{8})\s+"
    r"(?P<symbol>\S+)\s+(?P<rva>[0-9A-Fa-f]{16})\s+"
    r"(?:(?P<flags>(?:\S+\s+)*))?(?P<owner>\S+)\s*$"
)
SYMBOL_START_RE = re.compile(r"^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+")


@dataclass(frozen=True)
class SectionContribution:
    section: int
    start: int
    length: int
    name: str
    kind: str


@dataclass(frozen=True)
class Symbol:
    section: int
    offset: int
    symbol: str
    owner: str
    bucket: str
    library: str


def parse_owner(owner: str) -> tuple[str, str]:
    if ":" in owner:
        library, obj = owner.split(":", 1)
        return library, obj
    return "<project>", owner


def format_bytes(value: int) -> str:
    if value >= 1024 * 1024:
        return f"{value / (1024 * 1024):7.2f} MiB"
    return f"{value / 1024:7.1f} KiB"


def short_symbol(name: str, width: int = 96) -> str:
    return name if len(name) <= width else name[: width - 3] + "..."


def append_symbol(symbols: list[Symbol], raw_line: str) -> bool:
    symbol_match = SYMBOL_RE.match(raw_line)
    if not symbol_match:
        return False

    section = int(symbol_match.group("section"), 16)
    if section == 0:
        return True

    owner = symbol_match.group("owner")
    library, obj = parse_owner(owner)
    symbols.append(
        Symbol(
            section=section,
            offset=int(symbol_match.group("offset"), 16),
            symbol=symbol_match.group("symbol"),
            owner=owner,
            bucket=obj,
            library=library,
        )
    )
    return True


def read_map(path: Path) -> tuple[list[SectionContribution], list[Symbol]]:
    sections: list[SectionContribution] = []
    symbols: list[Symbol] = []
    in_symbols = False
    pending_symbol_line: str | None = None

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        section_match = SECTION_RE.match(raw_line)
        if section_match and not in_symbols:
            sections.append(
                SectionContribution(
                    section=int(section_match.group("section"), 16),
                    start=int(section_match.group("start"), 16),
                    length=int(section_match.group("length"), 16),
                    name=section_match.group("name"),
                    kind=section_match.group("kind"),
                )
            )
            continue

        if "Publics by Value" in raw_line or raw_line.strip() == "Static symbols":
            if pending_symbol_line is not None:
                append_symbol(symbols, pending_symbol_line)
                pending_symbol_line = None
            in_symbols = True
            continue

        if not in_symbols:
            continue

        if not raw_line.strip():
            if pending_symbol_line is not None:
                append_symbol(symbols, pending_symbol_line)
                pending_symbol_line = None
            continue

        if SYMBOL_START_RE.match(raw_line):
            if pending_symbol_line is not None:
                append_symbol(symbols, pending_symbol_line)
            pending_symbol_line = raw_line
            continue

        if pending_symbol_line is not None:
            pending_symbol_line += raw_line.strip()

    if pending_symbol_line is not None:
        append_symbol(symbols, pending_symbol_line)

    return sections, symbols


def estimate_symbol_sizes(
    sections: list[SectionContribution], symbols: list[Symbol]
) -> list[tuple[int, Symbol]]:
    section_lengths: dict[int, int] = {}
    for contribution in sections:
        section_lengths[contribution.section] = max(
            section_lengths.get(contribution.section, 0),
            contribution.start + contribution.length,
        )

    by_section: dict[int, list[Symbol]] = collections.defaultdict(list)
    for symbol in symbols:
        by_section[symbol.section].append(symbol)

    estimates: list[tuple[int, Symbol]] = []
    for section, section_symbols in by_section.items():
        ordered = sorted(section_symbols, key=lambda item: item.offset)
        end = section_lengths.get(section)
        if end is None:
            continue
        for index, symbol in enumerate(ordered):
            next_offset = ordered[index + 1].offset if index + 1 < len(ordered) else end
            size = max(0, next_offset - symbol.offset)
            if size:
                estimates.append((size, symbol))
    return estimates


def print_table(title: str, rows: list[tuple[int, str]], total: int, limit: int) -> None:
    print(title)
    if not rows:
        print("  <none>")
        return
    for size, name in rows[:limit]:
        share = (size / total * 100.0) if total else 0.0
        print(f"  {format_bytes(size)}  {share:5.1f}%  {name}")


def counter_rows(counter: collections.Counter[str]) -> list[tuple[int, str]]:
    return [(size, name) for name, size in counter.most_common()]


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize an MSVC linker map.")
    parser.add_argument("map_path", type=Path)
    parser.add_argument("--top", type=int, default=20, help="number of rows per section")
    args = parser.parse_args()
    if not args.map_path.exists():
        raise SystemExit(f"{args.map_path}: map file not found")

    sections, symbols = read_map(args.map_path)
    if not sections:
        raise SystemExit(f"{args.map_path}: no section table found")
    if not symbols:
        raise SystemExit(f"{args.map_path}: no symbol table found")

    section_total = sum(section.length for section in sections)
    estimated = estimate_symbol_sizes(sections, symbols)
    estimated_total = sum(size for size, _ in estimated)

    by_section_name: collections.Counter[str] = collections.Counter()
    by_object: collections.Counter[str] = collections.Counter()
    by_library: collections.Counter[str] = collections.Counter()
    by_project_object: collections.Counter[str] = collections.Counter()

    section_lookup = {section.section: section.name for section in sections}
    for section in sections:
        by_section_name[section.name] += section.length

    for size, symbol in estimated:
        by_object[symbol.bucket] += size
        by_library[symbol.library] += size
        if symbol.library == "<project>":
            by_project_object[symbol.bucket] += size

    largest_symbols = sorted(
        ((size, f"{short_symbol(symbol.symbol)}  [{symbol.owner}]") for size, symbol in estimated),
        reverse=True,
    )
    largest_code_symbols = sorted(
        (
            (size, f"{short_symbol(symbol.symbol)}  [{symbol.owner}]")
            for size, symbol in estimated
            if section_lookup.get(symbol.section, "").startswith(".text")
        ),
        reverse=True,
    )

    print(f"Map: {args.map_path}")
    print(f"Section bytes: {format_bytes(section_total).strip()}")
    print(f"Estimated symbol bytes: {format_bytes(estimated_total).strip()}")
    print("Note: symbol sizes are inferred from adjacent map addresses.")
    print()

    print_table("Largest sections", counter_rows(by_section_name), section_total, args.top)
    print()
    print_table("Largest libraries", counter_rows(by_library), estimated_total, args.top)
    print()
    print_table("Largest project objects", counter_rows(by_project_object), estimated_total, args.top)
    print()
    print_table("Largest objects overall", counter_rows(by_object), estimated_total, args.top)
    print()
    print_table("Largest inferred code symbols", largest_code_symbols, estimated_total, args.top)
    print()
    print_table("Largest inferred symbols overall", largest_symbols, estimated_total, args.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
