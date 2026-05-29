# Format Performance Journal

This document records formatter-only performance experiments for the `format-golden` benchmark. Keep entries focused on formatter data structures, allocation cost, CPU hotspots, and byte-for-byte golden-output safety.

## Current Target

- Benchmark: `build\CaseDashBenchmarks.exe format-golden 1000 2`
- Goal: keep `format_loop` under `20 ms` per iteration.
- Invariant: golden output stays byte-for-byte identical unless a test fixture intentionally records a formatter bug fix.

## Experiment Log

### Baseline After Syntax Scratch Marks

- Status: Current starting point for the under-20 ms target.
- Command: `build\CaseDashBenchmarks.exe format-golden 100 2`
- Result: `format_loop` `23.79 ms` per iteration.
- Splits: parse `11.55 ms`, print `11.63 ms`, tokenize `0.47 ms`, break model `3.68 ms`, solve `6.05 ms`, write `0.25 ms`.
- Notes: Previous work put syntax-node scratch storage behind a clearly marked block, removed repeated ancestor marking, combined list item metadata, and kept output comparison active.

### Daemon Command Repair

- Status: Kept for repeatable profiling.
- Command: `profile_benchmark.cmd format-golden 100 2`
- Result: daemon requests now accept `format-golden` and skip redundant developer-environment setup inside `/run-request` execution.
- Notes: This fixes the contradictory "unknown benchmark" path and avoids repeatedly extending the command environment in the elevated daemon process.

### Printer Column Cache

- Status: Kept.
- Command: `build\CaseDashBenchmarks.exe format-golden 100 2`
- Result: `format_loop` moved from `23.79 ms` to about `23.36 ms` per iteration.
- Notes: The printer tracks the current column as text is emitted instead of scanning backward through the output buffer for every column query. Golden output remains byte-for-byte identical.

### Dense Break Choices

- Status: Kept for allocation shape.
- Result: Runtime is roughly neutral, but `FormatBreakSolution` now stores choices in a dense vector indexed by break-node id instead of a `std::map`.
- Notes: The solver preserves first-choice-wins assignment order when flattening the choice tree.

### Inline Solver Frontier

- Status: Kept.
- Command: `build\CaseDashBenchmarks.exe format-golden 1000 2`
- Result: `format_loop` reached about `20.46 ms`; solve moved from about `6.05 ms` to about `4.34 ms`.
- Notes: The DP frontier uses an inline `NodeResults` buffer instead of allocating small temporary vectors for each candidate set.

### Raw Inline Solver Frontier

- Status: Kept.
- Command: `build\CaseDashBenchmarks.exe format-golden 1000 2`
- Result: solve moved to about `3.93 ms`; direct loop runs reached `19.98 ms` before the break-node span work.
- Notes: `NodeResults` uses raw inline storage so empty temporary frontiers do not eagerly construct and zero-fill unused `NodeResult` slots. The inline capacity is `8`, which avoids most spills without the old construction cost.

### Combined Printer Brace Stack

- Status: Kept.
- Result: Runtime is neutral to slightly positive and memory shape is simpler.
- Notes: Four parallel brace vectors became one `BraceFrame` vector with role, paren depth, restore indent, and close indent stored together.

### Arena-Backed Chain Spans

- Status: Kept.
- Command: `build\CaseDashBenchmarks.exe format-golden 1000 2`
- Result: final measured `format_loop` is `19.75 ms` per iteration with parse `10.52 ms`, print `8.80 ms`, break model `3.39 ms`, solve `3.92 ms`, and write `0.20 ms`.
- Notes: `FormatBreakNode` stores chain operands and operators as spans backed by the break-model arena. This removes two owning vectors from every break node and cuts per-chain allocations without changing emitted formatting.

### Final Daemon Profile

- Status: Informational.
- Command: `profile_benchmark.cmd format-golden 100 2`
- Request: `build\profile_benchmark_daemon\requests\2166_31915_28499`
- Result: profiled loop is `21.21 ms` with profiling overhead; parse `11.39 ms`, print `9.27 ms`, break model `3.47 ms`, solve `4.04 ms`.
- Hotspots: tree-sitter parse remains the largest inclusive cost; formatter-owned print time is concentrated in `Printer::FlushPendingTokens`, `BreakModelBuilder::BreakModelBuilder`, and `Solver::SolveChildrenAlternatives`.

### Reverted Experiments

- Broad one-line alternative pruning changed byte-for-byte golden output because a child that fits locally can still need a split to leave room for following siblings.
- Last-child alternative pruning also changed golden tie behavior and stayed reverted.
- Tree-sitter cursor child iteration, parser reset removal, print-token width caching, `NodeResult` field packing, memo pre-reserve, and folded common-root marking did not produce reliable wins on this benchmark.
