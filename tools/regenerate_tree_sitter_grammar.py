#!/usr/bin/env python3
"""Regenerate the vendored tree-sitter C++ grammar outputs."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path


TREE_SITTER_CLI_VERSION = "0.24.7"
TREE_SITTER_CLI_URL = (
    f"https://github.com/tree-sitter/tree-sitter/releases/download/v{TREE_SITTER_CLI_VERSION}/"
    "tree-sitter-windows-x64.gz"
)
TREE_SITTER_CLI_SHA512 = (
    "4CEFF1C79CF8491B1099CBC401AC4F2B85BAC45716C8C4B24C3EDA35A38C01E4996000CAF86979323E3F6352B2BF61CE2904C971"
    "627AFC4B0BCDEFD4E40C8A36"
)
MACRO_CATEGORY_ORDER = (
    "calling_convention",
    "raw_macro_function_prefix",
    "function_prefix",
    "macro_function_definition",
    "no_throw_macro",
    "name_macro_call",
    "namespace_alias_macro",
)
REQUIRED_MACRO_CATEGORIES = ("calling_convention",)
FORMAT_CATEGORY_KEYS = {
    "calling_convention": "CallingConvention",
    "raw_macro_function_prefix": "RawMacroFunctionPrefixes",
    "function_prefix": "FunctionPrefixes",
    "macro_function_definition": "MacroFunctionDefinitions",
    "no_throw_macro": "NoThrowMacros",
    "name_macro_call": "NameMacroCalls",
    "namespace_alias_macro": "NamespaceAliasMacros",
}
MACRO_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def strip_yaml_comment(line: str) -> str:
    in_single_quote = False
    in_double_quote = False
    result: list[str] = []
    for char in line:
        if char == "'" and not in_double_quote:
            in_single_quote = not in_single_quote
        elif char == '"' and not in_single_quote:
            in_double_quote = not in_double_quote
        elif char == "#" and not in_single_quote and not in_double_quote:
            break
        result.append(char)
    return "".join(result).strip()


def unquote_scalar(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == "'" and value[-1] == "'":
        return value[1:-1].replace("''", "'")
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return value


def load_macro_category_section(config_path: Path) -> dict[str, list[str]]:
    lines = config_path.read_text(encoding="utf-8").splitlines()
    macro_categories: dict[str, list[str]] = {}
    in_macro_section = False
    current_key: str | None = None
    for raw_line in lines:
        line = strip_yaml_comment(raw_line)
        if not line or line in {"---", "..."}:
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        if indent == 0:
            in_macro_section = line == "MacroCategories:"
            current_key = None
            continue
        if not in_macro_section:
            continue
        if indent == 2 and line.endswith(":"):
            current_key = line[:-1].strip()
            macro_categories.setdefault(current_key, [])
        elif indent >= 4 and current_key is not None and line.startswith("- "):
            macro_categories[current_key].append(unquote_scalar(line[2:]))
    return macro_categories


def clean_macro_category_names(config_path: Path, config_key: str, names: list[str]) -> list[str]:
    seen = set()
    clean_names: list[str] = []
    for name in names:
        if not MACRO_NAME_PATTERN.match(name):
            fail(f"{config_path} MacroCategories.{config_key} entry is not a C/C++ macro name: {name!r}")
        if name in seen:
            fail(f"{config_path} MacroCategories.{config_key} entry is duplicated: {name}")
        seen.add(name)
        clean_names.append(name)
    return clean_names


def load_macro_categories(config_path: Path) -> dict[str, list[str]]:
    macro_categories = load_macro_category_section(config_path)
    if not macro_categories:
        fail(f"{config_path} MacroCategories must be present")

    categories: dict[str, list[str]] = {}
    for category in MACRO_CATEGORY_ORDER:
        config_key = FORMAT_CATEGORY_KEYS[category]
        names = macro_categories.get(config_key, [])
        if category in REQUIRED_MACRO_CATEGORIES and not names:
            fail(f"{config_path} MacroCategories.{config_key} must be a non-empty array")
        clean_names = clean_macro_category_names(config_path, config_key, names)
        categories[category] = clean_names
    return categories


def load_macro_category_sources(config_paths: list[Path], repo_root: Path) -> list[tuple[str, dict[str, list[str]]]]:
    sources: list[tuple[str, dict[str, list[str]]]] = []
    for config_path in config_paths:
        categories = load_macro_categories(config_path)
        source_name = config_path.relative_to(repo_root).as_posix()
        sources.append((source_name, categories))
    return sources


def write_macro_config(output_path: Path, sources: list[tuple[str, dict[str, list[str]]]]) -> None:
    lines = [
        "// Generated from .cpp-format and tools/tests/format/.cpp-format-userver by",
        "// tools/regenerate_tree_sitter_grammar.py.",
        "module.exports = {",
        "  macro_categories: {",
    ]
    for category in MACRO_CATEGORY_ORDER:
        lines.append(f"    {category}: [")
        seen_names: set[str] = set()
        for source_name, categories in sources:
            lines.append(f"      // {source_name}")
            for name in categories[category]:
                if name in seen_names:
                    continue
                seen_names.add(name)
                lines.append(f"      {json.dumps(name)},")
        lines.append("    ],")
    lines.extend([
        "  },",
        "};",
        "",
    ])
    output_path.write_text("\n".join(lines), encoding="utf-8")


def ensure_tree_sitter_cli(repo_root: Path, tree_sitter_cli: str | None) -> Path:
    if tree_sitter_cli:
        cli_path = Path(tree_sitter_cli)
        if not cli_path.exists():
            fail(f"tree-sitter CLI was not found: {cli_path}")
        return cli_path

    if os.name != "nt":
        fail("Pass --tree-sitter-cli <path> on non-Windows hosts.")

    cli_dir = repo_root / "build" / f"tree-sitter-cli-v{TREE_SITTER_CLI_VERSION}"
    cli_path = cli_dir / "tree-sitter.exe"
    if cli_path.exists():
        return cli_path

    archive_path = cli_dir / "tree-sitter-windows-x64.gz"
    legacy_archive_path = repo_root / "build" / f"tree-sitter-windows-x64-v{TREE_SITTER_CLI_VERSION}.gz"
    cli_dir.mkdir(parents=True, exist_ok=True)
    if legacy_archive_path.exists():
        shutil.copyfile(legacy_archive_path, archive_path)
    elif not archive_path.exists():
        print(f"Downloading tree-sitter CLI v{TREE_SITTER_CLI_VERSION} to {archive_path}", flush=True)
        with urllib.request.urlopen(TREE_SITTER_CLI_URL, timeout=60) as response:
            archive_path.write_bytes(response.read())
    archive_hash = hashlib.sha512(archive_path.read_bytes()).hexdigest().upper()
    if archive_hash != TREE_SITTER_CLI_SHA512:
        archive_path.unlink(missing_ok=True)
        fail("Downloaded tree-sitter CLI archive hash did not match the pinned SHA512.")

    with gzip.open(archive_path, "rb") as compressed:
        cli_path.write_bytes(compressed.read())
    return cli_path


def run_tree_sitter_generate(cpp_grammar_dir: Path, vendor_root: Path, tree_sitter_cli: Path) -> None:
    env = os.environ.copy()
    existing_node_path = env.get("NODE_PATH")
    env["NODE_PATH"] = str(vendor_root) if not existing_node_path else str(vendor_root) + os.pathsep + existing_node_path

    subprocess.run([str(tree_sitter_cli), "generate"], cwd=cpp_grammar_dir, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tree-sitter-cli",
        help="Optional path to an existing tree-sitter CLI executable. Defaults to a pinned CLI under build/.",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    vendor_root = repo_root / "src" / "tools" / "vendor" / "tree-sitter"
    cpp_grammar_dir = vendor_root / "tree-sitter-cpp"
    c_grammar_dir = vendor_root / "tree-sitter-c"
    config_paths = [
        repo_root / ".cpp-format",
        repo_root / "tools" / "tests" / "format" / ".cpp-format-userver",
    ]
    macro_config_path = cpp_grammar_dir / "macro_config.js"

    for required_path in (cpp_grammar_dir / "grammar.js", c_grammar_dir / "grammar.js", *config_paths):
        if not required_path.exists():
            fail(f"Missing required grammar regeneration input: {required_path}")

    sources = load_macro_category_sources(config_paths, repo_root)
    write_macro_config(macro_config_path, sources)
    tree_sitter_cli = ensure_tree_sitter_cli(repo_root, args.tree_sitter_cli)
    run_tree_sitter_generate(cpp_grammar_dir, vendor_root, tree_sitter_cli)
    print(f"Regenerated tree-sitter C++ grammar outputs under {cpp_grammar_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
