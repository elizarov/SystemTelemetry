---
name: benchmark-performance-review
description: "Use only when explicitly invoked: review CaseDash benchmark performance."
---

# Benchmark Performance Review

## Overview

Use this skill to verify that CaseDash performance still matches the maintained benchmark baselines, then fix repeatable regressions with measured, narrowly scoped changes.

## Required Context

1. Read `AGENTS.md`.
2. Read `docs/profile_benchmark.md` before forming conclusions. Treat it as the source of truth for benchmark commands, current baselines, confirmed hotspots, kept optimizations, rejected experiments, and practical guidance.
3. Read `docs/build.md` for build and validation entrypoints.
4. Read relevant architecture docs before changing subsystem boundaries:
   - `docs/architecture.md`
   - `docs/architecture/*.md` for the touched package
5. Read owning behavior docs before changing user-visible behavior, diagnostics output, provider behavior, layout-edit behavior, or layout-guide-sheet behavior.

## Baseline Review

1. Inspect `git status --short`; ignore unrelated dirty work and do not revert changes made by others.
2. Build a fresh benchmark executable:

```powershell
.\build.cmd /benchmarks
```

3. Run every maintained direct benchmark listed in the `Benchmark Workflow` section of `docs/profile_benchmark.md` sequentially from the fresh build. Do not duplicate the command list here; `docs/profile_benchmark.md` is the owning source of truth.

4. Compare direct-run timing lines with the `Current Known Baseline` section in `docs/profile_benchmark.md`. Compare like with like: primary loop metrics, named submetrics, same benchmark name, same iteration count, and same warmup count.
5. Treat a single noisy run as inconclusive. When a metric looks slow, rerun the same direct benchmark at least twice and compare the median or the stable range.
6. Treat a regression as actionable when the primary loop metric or an important submetric is repeatably worse than the documented current baseline beyond normal noise, especially when a hot UI-path metric moves by roughly `10%` or more, a guide-sheet metric moves by several milliseconds, or the regression changes the known hotspot shape.
7. Use direct `build\CaseDashBenchmarks.exe` runs as the repeatable baseline. Use `profile_benchmark.cmd` timings only as profiler-instrumented confirmation, not as the baseline comparison.

## Regression Investigation

1. Isolate the changed benchmark and the first metric that moved. Prefer the smallest repeatable direct benchmark loop while investigating.
2. Review the matching sections in `docs/profile_benchmark.md`:
   - `Current Confirmed Hotspots`
   - `Kept Optimizations`
   - `Tested Hypotheses`
   - `Practical Guidance For Future Experiments`
3. Do not retry experiments that the log rejects unless surrounding code changed enough to make the old result stale. Record why the retry is justified.
4. Capture a benchmark-focused profile when direct runs show a material regression or when hotspot attribution is needed. Use the matching `profile_benchmark.cmd` command documented in the `Benchmark Workflow` section of `docs/profile_benchmark.md`.

5. For repeated unattended profiling, start the elevated daemon once with `.\profile_benchmark.cmd /daemon-start`, then issue ordinary `profile_benchmark.cmd <benchmark> ...` requests.
6. Prefer fixes that reduce real hot-path work, preserve the documented benchmark harness semantics, and keep benchmark-sensitive code on the maintained Release optimization profile.
7. Keep custom hash tables or similar low-level performance utilities in separate owning `.h`/`.cpp` modules, not embedded in large renderer, provider, or controller files.

## Fix Discipline

1. Change one coherent hypothesis at a time.
2. Keep edits scoped to the owning subsystem and the regressed benchmark path.
3. Preserve user-visible behavior unless the user explicitly asks for a behavior change; update the owning behavior docs when behavior changes.
4. Rebuild and rerun the affected benchmark after each serious experiment.
5. If an experiment regresses, is neutral without improving code clarity, or contradicts the benchmark log, revert only your own experiment changes and record the rejected result in `docs/profile_benchmark.md`.
6. Keep retained optimizations only when direct benchmark data supports them and the final validation set remains healthy.
7. Add concise comments only where a non-obvious code shape preserves a measured performance invariant.

## Final Validation

- Run `.\format.cmd changed` after C++ edits; use `.\format.cmd fix changed` if formatting needs repair.
- Run `.\build.cmd /benchmarks` after the final retained changes.
- Rerun all maintained direct benchmarks listed in `docs/profile_benchmark.md` after the final build.
- Run `.\test.cmd` when production behavior, shared logic, or benchmark harness behavior changes.
- Run the smallest headless diagnostics command from `docs/diagnostics.md` when a fix touches diagnostics, rendering exports, app icon generation, layout-edit output, provider behavior, config reload, or layout-guide-sheet behavior. Put explicit outputs under `build\` and add `/default-config` when exercising built-in config.

## Documentation

Update `docs/profile_benchmark.md` when:

- A benchmark baseline changes and the new result becomes the maintained current-tree baseline.
- A retained optimization changes benchmark performance or hotspot shape.
- A rejected or reverted experiment should not be repeated.
- A new recurring performance pitfall is discovered.

Keep entries present-tense and concise. Record commands, representative timing lines, hotspot evidence when used, retained or rejected conclusion, and the reason the result matters.

Update other maintained docs only when behavior, commands, architecture boundaries, diagnostics contracts, provider requirements, or build validation rules change.

## Output

Summarize:

- Build command and benchmark commands run.
- Baseline comparison for every benchmark, including any repeated noisy runs.
- Regressions found, root cause, and files changed.
- Final benchmark timing lines and whether they are at or better than the maintained baseline.
- Profiling evidence used, if any.
- Docs and validation commands run.
