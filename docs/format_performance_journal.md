# Format Performance Journal

This note records formatter-only `format-golden` and `format-all` performance work. The golden output remains byte-for-byte stable except for intentional fixture updates that capture formatter bug fixes.

## Target

- Golden command: `build\CaseDashBenchmarks.exe format-golden 1000 2`.
- All-source command: `build\CaseDashBenchmarks.exe format-all 5 2`.
- Goal: keep `format-golden` `format_loop` under `20 ms` per iteration, keep interactive all-source checks parallel by default, and keep `format-all` benchmark output focused on serial formatter CPU work.

## Validated Result

- Best historical `format-golden` direct run: `19.75 ms` per iteration. Current direct check after all-source work is about `24.49 ms` per iteration on the Gigabyte X570 I profile machine, with parse `13.21 ms`, print `10.46 ms`, break model `3.92 ms`, solve `4.42 ms`, and write `0.23 ms`.
- Best historical parallel `format-all` direct run: `115.31 ms` per full project pass for `405` processed files and `17` ignored files. The maintained `format-all` benchmark now forces concurrency `1` so its phase splits measure total formatter CPU work rather than parallel wall-clock throughput.
- Current `format.cmd` wrapper check reports the formatter work as `123 ms` for the same `405` processed files after git file discovery and tool startup.
- Daemon profile requests: sequential all-source baseline `build\profile_benchmark_daemon\requests\6472_30541_6518`; retained parallel all-source result `build\profile_benchmark_daemon\requests\7781_15275_26626`; historical golden profile `build\profile_benchmark_daemon\requests\2166_31915_28499`; model-build reduction profiles `build\profile_benchmark_daemon\requests\24626_32284_24894`, `build\profile_benchmark_daemon\requests\25025_101_8866`, `build\profile_benchmark_daemon\requests\28179_28374_29688`, `build\profile_benchmark_daemon\requests\30093_2716_12333`, `build\profile_benchmark_daemon\requests\32493_5707_2531`, `build\profile_benchmark_daemon\requests\215_12336_27948`, `build\profile_benchmark_daemon\requests\594_13967_3040`, `build\profile_benchmark_daemon\requests\1469_10956_6510`, `build\profile_benchmark_daemon\requests\7011_31993_11609`, and current public-API profile `build\profile_benchmark_daemon\requests\8483_29861_7422`.
- Remaining hotspots: tree-sitter parse, public tree-sitter child lookup, `Printer::FlushPendingTokens`, and solver alternatives. Serial `format-all` phase totals now line up with wall-clock `format_loop`, while interactive tool runs stay parallel by default. Current public-API `format-all 3 2` checks are about `759-763 ms` per iteration, with `format_parse` about `541-543 ms`, or about `71%` of `format_loop`. The current public-API all-source profile has `BuildFormatModel` at `13.60%` inclusive, with `ts_node__child` at `8.01%` exclusive.

## Experiments

| Change | Result |
| --- | --- |
| Baseline after syntax scratch marks | `23.79 ms`; solve `6.05 ms`. |
| Daemon command repair | `format-golden` profiles run reliably through the daemon. |
| Printer column cache | About `23.36 ms`; avoids backward output scans for column queries. |
| Dense break choices | Runtime neutral; removes map allocation and lookup from emitted choices. |
| Inline solver frontier | About `20.46 ms`; solve moved to about `4.34 ms`. |
| Raw inline solver frontier | About `19.98 ms`; solve moved to about `3.93 ms`. |
| Combined brace stack | Runtime neutral to slightly positive; replaces four parallel vectors with `BraceFrame`. |
| Arena-backed chain spans | Final `19.75 ms`; break model `3.39 ms`, solve `3.92 ms`. |
| All-source benchmark | Adds `format-all`, which discovers maintained `.cpp` and `.h` files under `src` and `tests`, applies `.cpp-format-ignore`, formats in memory, and reports whether all processed files already match formatter output. |
| All-source daemon profile | Sequential baseline was parse-bound: `format_loop` `1022.35 ms`, parse `726.98 ms`, print `202.93 ms`, solve `65.51 ms`; call tree centered on `ParseFormatModel`, tree-sitter parse, `BuildFormatModel`, and `Printer::FlushPendingTokens`. |
| Per-file parallel formatting | Retained for `format.cmd` and `CaseDashTools.exe format`; the `format-all` benchmark uses concurrency `1` to measure serial CPU work. Golden fixture output remains byte-for-byte stable under `tools\tests\format\format_test.py`. |
| Vector-backed format nodes | Replaces the format-model node deque with a pre-reserved vector and removes redundant parent/depth repairs during known local rewrites. This keeps golden output stable and trims some allocator work, but tree-sitter child traversal remains the dominant model-build cost. |
| Tree-sitter symbol classification | Builds formatter syntax-kind tables from tree-sitter symbol ids and uses `ts_node_symbol()` on the hot model-building path, with text lookup retained as a fallback for duplicate anonymous token spellings. This is a small generic win and avoids string hash lookup for most syntax nodes. |
| Optimized generated C++ parser | The vendored C++ grammar had MSVC optimization disabled by a generated `#pragma`. Re-enabling optimization keeps formatter tests stable and moves direct `format-all 3 2` from roughly `940 ms` per iteration to about `879-897 ms`; parse is still about two thirds of the benchmark. |
| Generic wrapper roles | Classifies transparent wrappers, whole-token wrappers, identifier-like atoms, simple unary/update atoms, field atoms, and compact empty delimiter groups by tree-sitter symbol role. Golden output stays stable after excluding template parameter lists and function parameter lists, which own spacing around `template <>` and trailing cv-qualifiers. Direct `format-all 3 2` moves to roughly `780 ms`, but sampled `BuildFormatModel` remains around `15%` because the remaining structural lists and statements still require child traversal. |
| Combined symbol info table | Merges tree-kind, token-kind, and wrapper-role lookup into one per-symbol table for the model builder. This keeps golden output stable and gives the best direct `format-all 3 2` check seen in this pass at about `765 ms`, though daemon samples still put `BuildFormatModel` near `15%`. |
| Child-vector PMR arena | Uses a monotonic resource for per-node child pointer vectors and keeps source formatting stable. The direct phase split stays around `20-21 ms` for format-model construction and the latest daemon sample still puts `BuildFormatModel` around `15%`, so allocation cleanup is not enough to meet the `10%` target. |
| Direct token/start fast paths | Uses public tree-sitter node APIs for symbol, start, and end lookups and returns known anonymous tokens before asking for child count. The change is behavior-preserving, but the fresh all-source profile remains child-traversal bound. |
| Public API backout | Removes the private tree-sitter layout mirror and returns model construction to public `ts_node_*` APIs only. Golden output stays stable; direct `format-all 3 2` is about `763 ms`, the daemon run is about `759 ms`, and `BuildFormatModel` returns to `13.60%` inclusive in `build\profile_benchmark_daemon\requests\8483_29861_7422`. |

## Reverted Work

- One-line alternative pruning changed golden output because local fits can still need later splits.
- Tree-sitter cursor iteration, parser reset removal, print-token width caching, `NodeResult` field packing, memo pre-reserve, and folded common-root marking did not produce reliable wins. The latest cursor retry replaced `ts_node__child` samples with `ts_tree_cursor_goto_*` samples and slightly regressed direct `format-all`, so indexed child traversal remains the retained path.
- One-pass visible-child iteration through mirrored tree-sitter internals reduced `BuildFormatModel` below `10%`, but was removed to keep formatter model construction on public tree-sitter APIs only.
- A selective cursor retry for wider child lists also failed to beat indexed child traversal.
- Transparent flattening of unknown tree-sitter wrappers changed formatting because statement boundaries, call-expression wrappers, unary pointer expressions, and nested parenthesized expressions carry context that downstream generic rules still need.
- Flattening statement wrappers such as `expression_statement` and `return_statement` changed control-body normalization because the wrapper is needed to brace the whole statement instead of only its final token or expression.
- Pruning blank-line trivia outside the printer's final preserving parent set changed golden output because trivia can move through intermediate structural nodes before the final print context is known.
