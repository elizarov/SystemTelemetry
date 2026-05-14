---
name: optimize-size
description: "Use only when explicitly invoked: reduce CaseDash executable size."
---

# Optimize Size

## Overview

Use this skill to run measured, high-leverage executable-size experiments for CaseDash. Aim for a retained `build\CaseDash.exe` reduction of at least 10 KiB when plausible, and keep going past the first small win while credible larger hypotheses remain.

## Required Context

1. Read `AGENTS.md`.
2. Read `docs/optimize_size.md` before forming hypotheses. Treat it as the source of truth for size constraints, retained decisions, rejected experiments, map workflow, current baseline, and size pitfall notes.
3. Read `docs/build.md` for build and validation entrypoints.
4. Read `docs/profile_benchmark.md` before changing renderer, widget layout, layout resolver, telemetry, provider sampling, or other hot paths.
5. Read `docs/architecture.md` and the relevant `docs/architecture/*.md` files before removing or bypassing package boundaries, middle-man layers, or shared abstractions.

## Baseline

1. Inspect `git status --short`; ignore unrelated dirty work and do not revert changes made by others.
2. Build a fresh Release app with `.\build.cmd`.
3. Record the shipped executable size:

```powershell
(Get-Item .\build\CaseDash.exe).Length
```

4. Generate maps with `.\build_maps.cmd`.
5. Inspect `build\CaseDash.map.summary.txt`. Use focused ad hoc map analysis when needed:

```powershell
python .\tools\analyze_link_map.py .\build\CaseDash.map --top 25
```

6. Compare the largest sections, project objects, and symbols against the maintained notes in `docs/optimize_size.md`. Treat MSVC map symbol sizes as estimates.

## Hypothesis Selection

Prioritize changes that can delete whole families of generated code, metadata, unwind data, resources, descriptors, or repeated cold-path machinery. Look for:

- Repeated code patterns across multiple source files or branches of one large method.
- Large methods with repeated construction, formatting, tracing, or validation chains.
- Inefficient metadata layouts, duplicated descriptor tables, redundant field facts, or extra data stored in maps.
- Repeated construction or copying of large structures, especially config, layout, telemetry, dump, and edit-tree payloads.
- APIs whose many parameters cause repeated call-site code, wide/narrow string churn, or template instantiations.
- Small fixed domains still using broad STL containers, sorting, callbacks, type erasure, or local algorithm variants.
- Middle-man abstractions that can be removed only when package boundaries and `lint.cmd` architecture checks allow it.

Do not spend the main effort on tiny localized cleanups unless they are part of a broader measured pattern or unblock a larger deletion. Avoid experiments already rejected in `docs/optimize_size.md` unless new surrounding code makes the old measurement stale, and record why the retry is justified.

## Experiment Discipline

1. Rank hypotheses by expected executable delta, behavioral risk, performance risk, and blast radius.
2. Change one coherent hypothesis at a time.
3. Prefer cold-path, resource, metadata, and linker-shape wins before changing renderer, widget draw, layout resolver, or telemetry hot paths.
4. Preserve the full feature set: supported hardware providers, diagnostics, layout-edit, localization, embedded default config/localization, app icon behavior, and `/default-config` behavior.
5. Keep normal builds free of map generation; use `build_maps.cmd` only for size analysis.
6. Rebuild and remeasure after each serious experiment. If an experiment is neutral or regresses and does not improve source clarity enough to keep, revert only your own changes from that experiment.
7. Leave concise `Size:` comments near non-obvious retained code shapes where a conventional cleanup would likely reintroduce larger code.
8. Keep going until the retained executable reduction reaches at least 10 KiB, or until the remaining plausible hypotheses are too risky, already disproven, or only tiny localized wins.

## Validation

- Run `.\format.cmd changed` after C++ edits; use `.\format.cmd fix changed` if formatting needs repair.
- Run `.\lint.cmd tidy changed` after retained C++ refactorings or code-shape rewrites, because size-oriented reshaping often breaks architecture, source-policy, include-style, or clang-tidy checks.
- Treat the previous bullet as the optimize-size skill's explicit, scoped tidy request despite the repository's blanket local `lint.cmd tidy` avoidance rule. Do not run the full unscoped `.\lint.cmd tidy` unless the user explicitly asks for that broader sweep.
- Run `.\build.cmd` before every shipped-size measurement and before headless validation.
- Run `.\build_maps.cmd` after the final retained build and inspect `build\CaseDash.map.summary.txt`.
- Run `.\test.cmd` when source behavior changes beyond trivial code-shape cleanup.
- Prefer the smallest headless app validation that proves the edited paths. For broad size work, use `/default-config /fake /exit` and put explicit diagnostics outputs under `build\`, for example:

```powershell
.\build\CaseDash.exe /default-config /fake /exit /trace:build\size_optimization_validation_trace.txt /dump:build\size_optimization_validation_dump.txt /screenshot:build\size_optimization_validation_screenshot.png /layout-guide-sheet:build\size_optimization_validation_sheet.png /app-icon:build\size_optimization_validation_app_icon.png /app-icon-size:64 /save-full-config:build\size_optimization_validation_full_config.ini
```

- If a change can affect performance-sensitive code, build the benchmark target with `.\build.cmd Release /benchmarks` and follow `docs/profile_benchmark.md`.

## Documentation

Update `docs/optimize_size.md` when an experiment is retained, rejected, neutral, or reveals a recurring project-specific pitfall. Keep the entry present-tense and concise, with baseline/final executable sizes and the important section or object deltas when useful.

Update other maintained docs only when behavior, commands, architecture boundaries, diagnostics behavior, or provider requirements change.

## Output

Summarize:

- Baseline and final `build\CaseDash.exe` sizes, plus the byte delta.
- The hypotheses tried, including retained and rejected/neutral experiments.
- The main map evidence that explains the final result.
- Files changed and validation commands run.
- Any remaining high-value hypotheses left for the next pass.
