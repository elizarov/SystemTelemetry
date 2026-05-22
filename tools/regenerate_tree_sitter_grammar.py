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
REQUIRED_MACRO_CATEGORIES = ("calling_convention",)
MACRO_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def load_macro_categories(config_path: Path) -> dict[str, list[str]]:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("schema_version") != 1:
        fail("tools/format_config.json schema_version must be 1")
    macro_categories = config.get("macro_categories")
    if not isinstance(macro_categories, dict):
        fail("tools/format_config.json macro_categories must be an object")

    categories: dict[str, list[str]] = {}
    for category in REQUIRED_MACRO_CATEGORIES:
        names = macro_categories.get(category)
        if not isinstance(names, list) or not names:
            fail(f"tools/format_config.json macro_categories.{category} must be a non-empty array")
        seen = set()
        clean_names: list[str] = []
        for name in names:
            if not isinstance(name, str) or not MACRO_NAME_PATTERN.match(name):
                fail(f"tools/format_config.json macro {category} entry is not a C/C++ macro name: {name!r}")
            if name in seen:
                fail(f"tools/format_config.json macro {category} entry is duplicated: {name}")
            seen.add(name)
            clean_names.append(name)
        categories[category] = clean_names
    return categories


def write_case_dash_macro_config(output_path: Path, categories: dict[str, list[str]]) -> None:
    lines = [
        "// Generated from tools/format_config.json by tools/regenerate_tree_sitter_grammar.py.",
        "module.exports = {",
        "  macro_categories: {",
    ]
    for category in REQUIRED_MACRO_CATEGORIES:
        lines.append(f"    {category}: [")
        for name in categories[category]:
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
    config_path = repo_root / "tools" / "format_config.json"
    macro_config_path = cpp_grammar_dir / "case_dash_macro_config.js"

    for required_path in (cpp_grammar_dir / "grammar.js", c_grammar_dir / "grammar.js", config_path):
        if not required_path.exists():
            fail(f"Missing required grammar regeneration input: {required_path}")

    categories = load_macro_categories(config_path)
    write_case_dash_macro_config(macro_config_path, categories)
    tree_sitter_cli = ensure_tree_sitter_cli(repo_root, args.tree_sitter_cli)
    run_tree_sitter_generate(cpp_grammar_dir, vendor_root, tree_sitter_cli)
    print(f"Regenerated tree-sitter C++ grammar outputs under {cpp_grammar_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
