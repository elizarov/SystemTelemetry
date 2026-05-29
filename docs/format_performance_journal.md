# Format Performance Journal

This note records formatter-only `format-golden` performance work. The golden output remains byte-for-byte stable except for intentional fixture updates that capture formatter bug fixes.

## Target

- Command: `build\CaseDashBenchmarks.exe format-golden 1000 2`
- Goal: keep `format_loop` under `20 ms` per iteration.

## Validated Result

- Best direct run: `19.75 ms` per iteration.
- Splits: parse `10.52 ms`, print `8.80 ms`, break model `3.39 ms`, solve `3.92 ms`, write `0.20 ms`.
- Daemon profile request: `build\profile_benchmark_daemon\requests\2166_31915_28499`.
- Remaining hotspots: tree-sitter parse, `Printer::FlushPendingTokens`, `BreakModelBuilder::BreakModelBuilder`, and `Solver::SolveChildrenAlternatives`.

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

## Reverted Work

- One-line alternative pruning changed golden output because local fits can still need later splits.
- Tree-sitter cursor iteration, parser reset removal, print-token width caching, `NodeResult` field packing, memo pre-reserve, and folded common-root marking did not produce reliable wins.
