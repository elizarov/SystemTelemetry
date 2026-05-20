---
name: ci-failures-review
description: "Use only when explicitly invoked: investigate CaseDash CI failures, especially pull request Validation failures."
---

# CI Failures Review

## Overview

Use this skill to investigate a failed workflow from the pull request or failing run outward, then make the smallest repository change that addresses all actionable diagnostics.

## Workflow

1. Read repository instructions first.
   - Start with `AGENTS.md`.
   - Consult `docs/build.md` for validation entrypoints and workflow behavior.
   - Consult `docs/diagnostics.md` when the failure involves diagnostics output or headless app validation.

2. Identify the failed PR check or run.
   - Treat pull requests as the normal entrypoint for feature-branch validation. Use the available GitHub tooling, usually `gh pr view`, `gh pr checks`, `gh run view`, and `gh run download`.
   - If the user gives only a branch name, find the open PR for that branch before listing runs. Feature-branch pushes do not start routine `Validation` runs.
   - Record the workflow name, event, PR number when present, base branch, head branch, run id, head commit, failed job, and failed step before editing.
   - For `pull_request` runs, remember that the checked-out code is the PR merge ref by default. Compare both the PR head SHA and the run SHA or merge ref with local `HEAD` and branch status; note when the checkout is ahead of or different from the failed commit.
   - For `push` runs, expect them primarily on `main` after merge. Do not look for a duplicate feature-branch push run unless the workflow has changed again.
   - Use failed-step logs and uploaded artifacts.

3. Diagnose.
   - Find actionable errors in the failed step.
   - Download uploaded reports when the workflow provides them, such as `clang-tidy-report`.
   - For clang-tidy reports, search the artifact for reportable `warning:` and `error:` lines; ignore progress lines and `clang-tidy exit code ignored after filtering` unless reportable output remains nearby.
   - Cross-check the reported file against the current worktree before patching because the branch may already be ahead of the failed commit.
   - For `misc-include-cleaner` reports on Win32 umbrella headers, verify that a direct replacement header is self-contained under clang-tidy before changing source. If a direct Windows SDK header fails under clang-tidy or the project policy expects the umbrella include, add a narrow `file|header` entry to the maintained allowlist in `tools/run_clang_tidy.ps1` instead of adding local `NOLINT`.
   - Ignore unrelated dirty work and do not revert changes made by others.

4. Patch the owning source.
   - Keep edits scoped to the failed diagnostic and the owning module.
   - When the owning module is lint tooling, keep allowlist changes exact and explain why the source include remains correct.
   - Update docs only when the failure or fix changes documented behavior, commands, or a recurring project pitfall.
   - When the investigation reveals a reusable CI pitfall or workflow lesson, update this skill's Lessons Learned section before the final response so the next run starts smarter.

## Lessons Learned

- Some direct Windows SDK headers are not self-contained under the CaseDash build defines. For example, replacing `windows.h` with `sysinfoapi.h` for `SYSTEMTIME` and `GetLocalTime` can fail with `winnt.h` reporting `No Target Architecture`. In that case, keep `windows.h` in source and add a precise include-cleaner allowlist entry.
- `profileapi.h` is also not self-contained for `LARGE_INTEGER`, `QueryPerformanceFrequency`, and `QueryPerformanceCounter` under the CaseDash build defines; keep `windows.h` and allowlist the include-cleaner finding when `windows.h` is the narrow working source include.
- If a clang-tidy fix only changes `tools/run_clang_tidy.ps1`, `lint.cmd tidy changed` can legitimately report no eligible changed project source or header files. Treat that as validation of the lint entrypoint and script parsing, not as proof that a full tidy sweep ran.
- When local `HEAD` is ahead of the failed run SHA, run `format.cmd` against the current worktree before treating the CI failure as isolated. `format.cmd fix changed` only repairs dirty changed files; use `format.cmd fix` when already-committed branch files also block the full format check.
- Clang-tidy can emit `Processing file ...` progress lines before returning a non-zero code for otherwise filtered include-cleaner diagnostics. Those progress lines are not reportable diagnostics; filter them before deciding the result still failed.
- Visual Studio LLVM clang-format versions can disagree on C++/CLI managed handles and `for each` syntax. Keep mixed-mode bridge `.cpp` formatter exclusions narrow and filename-based instead of rewriting managed bridge source to satisfy one runner version.
- A docs-only or website-only commit can still fail the benchmark build when earlier source-list drift is latent. If `CaseDashBenchmarks` links a source that calls newly factored production helpers, verify the helper `.cpp` is also listed in that benchmark target, not only in the app or test target.
- `lint.cmd tidy changed` runs the normal lint gates before the changed-file clang-tidy slice. If source-policy failures appear there, fix them even when the downloaded CI artifact only reports clang-tidy diagnostics.
- PowerShell 7 treats negative `-split` max-substring counts as right-to-left splitting. Release tooling that needs normal line splitting should use max count `0` or omit the max count so CI `pwsh` sees the same chunks as Windows PowerShell.
- CaseDash validates feature-branch changes through pull requests. A failed PR `Validation` check is the canonical signal to investigate; a same-commit branch-push run is not expected for ordinary feature work.

## Local Validation

- Run `.\format.cmd`; if formatting is required, run `.\format.cmd fix changed`.
- Build through `.\build.cmd` before validation that needs a fresh compile database or executable. Match the workflow option, such as `.\build.cmd /benchmarks`, only when it materially matters to the failure.
- For clang-tidy CI failures, run only `.\lint.cmd tidy changed` locally.
- If a clang-tidy fix only changes lint tooling or an allowlist, `.\lint.cmd tidy changed` may report no eligible changed project source or header files. Treat that as validation of the lint entrypoint and script parsing, not proof that the full tidy sweep ran, and say so in the output.
- Do not run full `.\lint.cmd tidy` unless the user explicitly asks for the full tidy sweep.
- Prefer the smallest additional workflow step that proves the fix. Run `.\test.cmd`, `.\package.cmd`, or diagnostics commands only when the failure or edited code path calls for them.

## Output

Summarize:

- Failed workflow run id, commit, job, and step reviewed.
- PR number, base branch, and head branch when the failure came from a pull request.
- Root cause and files changed.
- Whether local `HEAD` matched the failed CI commit, PR head, or PR merge ref, or was ahead of them.
- Validation commands run, including any command intentionally skipped because the changed-file validation policy applies.
