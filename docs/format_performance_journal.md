# Format Performance Journal

This note records formatter-only `format-golden` and `format-all` performance work. The golden output remains byte-for-byte stable except for intentional fixture updates that capture formatter bug fixes.

## Target

- Golden command: `build\CaseDashBenchmarks.exe format-golden 1000 2`.
- All-source command: `build\CaseDashBenchmarks.exe format-all 5 2`.
- Goal: keep `format-golden` `format_loop` under `20 ms` per iteration and make the all-source formatting check at least `30%` faster than the sequential all-file baseline.

## Validated Result

- Best historical `format-golden` direct run: `19.75 ms` per iteration. Current direct check after all-source work is about `24.49 ms` per iteration on the Gigabyte X570 I profile machine, with parse `13.21 ms`, print `10.46 ms`, break model `3.92 ms`, solve `4.42 ms`, and write `0.23 ms`.
- Best `format-all` direct run: `115.31 ms` per full project pass for `405` processed files and `17` ignored files. The sequential baseline before per-file worker parallelism was `1022.35 ms`, so the retained all-source check is about `88.7%` faster.
- Current `format.cmd` wrapper check reports the formatter work as `123 ms` for the same `405` processed files after git file discovery and tool startup.
- Daemon profile requests: sequential all-source baseline `build\profile_benchmark_daemon\requests\6472_30541_6518`; retained parallel all-source result `build\profile_benchmark_daemon\requests\7781_15275_26626`; historical golden profile `build\profile_benchmark_daemon\requests\2166_31915_28499`.
- Remaining hotspots: tree-sitter parse, `Printer::FlushPendingTokens`, `BuildFormatModel`, and token spacing. `format-all` phase totals are cumulative across workers, so parse and print splits can exceed wall-clock `format_loop`.

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
| Per-file parallel formatting | Retained. `format-all` direct run dropped to `115.31-117.44 ms`; `format.cmd` check reports `117-123 ms` formatter work for `405` processed files. Golden fixture output remains byte-for-byte stable under `tools\tests\format\format_test.py`. |

## Reverted Work

- One-line alternative pruning changed golden output because local fits can still need later splits.
- Tree-sitter cursor iteration, parser reset removal, print-token width caching, `NodeResult` field packing, memo pre-reserve, and folded common-root marking did not produce reliable wins.
