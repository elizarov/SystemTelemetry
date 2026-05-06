---
name: ci-failures-review
description: Review and fix CI or GitHub Actions workflow failures for this repository, especially requests to inspect the latest failed Validation workflow on a branch, download failed-step logs or artifacts, identify the first actionable diagnostic, patch the code narrowly, and run only the minimal local validation needed for the changed files.
---

# CI Failures Review

## Overview

Use this skill to investigate a failed workflow from the failing run outward, then make the smallest repository change that addresses the first actionable diagnostic.

## Workflow

1. Read repository instructions first.
   - Start with `AGENTS.md`.
   - Consult `docs/build.md` for validation entrypoints and workflow behavior.
   - Consult `docs/diagnostics.md` only when the failure involves diagnostics output or headless app validation.

2. Identify the failed run.
   - Use the user-requested incident tool first, such as `dh`, when it is available.
   - If the requested tool is unavailable, say so briefly and use the available GitHub tooling, usually `gh run list`, `gh run view`, and `gh run download`.
   - Record the workflow name, branch, run id, head commit, failed job, and failed step before editing.
   - Prefer failed-step logs and uploaded artifacts over guessing from the final workflow conclusion.

3. Diagnose narrowly.
   - Find the first actionable error in the failed step.
   - Download uploaded reports when the workflow provides them, such as `clang-tidy-report`.
   - Cross-check the reported file against the current worktree before patching because the branch may already be ahead of the failed commit.
   - Ignore unrelated dirty work and do not revert changes made by others.

4. Patch the owning source.
   - Keep edits scoped to the failed diagnostic and the owning module.
   - Use `apply_patch` for manual edits.
   - Update docs only when the failure or fix changes documented behavior, commands, or a recurring project pitfall.

## Local Validation

- In PowerShell, run repository `.cmd` entrypoints with `.\`, for example `.\format.cmd`.
- Run `.\format.cmd`; if formatting is required, run `.\format.cmd fix changed`, then rerun `.\format.cmd`.
- Build through `.\build.cmd` before validation that needs a fresh compile database or executable. Match the workflow option, such as `.\build.cmd /benchmarks`, only when it materially matters to the failure.
- For clang-tidy CI failures, run only `.\lint.cmd tidy changed` locally.
- Do not run full `.\lint.cmd tidy` unless the user explicitly asks for the full tidy sweep.
- If a long validation command is interrupted, check for leftover child processes from that command and stop only that command tree before continuing.
- Prefer the smallest additional workflow step that proves the fix. Run `.\test.cmd`, `.\package.cmd`, or diagnostics commands only when the failure or edited code path calls for them.

## Output

Summarize:

- Failed workflow run id, commit, job, and step reviewed.
- Root cause and files changed.
- Validation commands run, including any command intentionally skipped because the changed-file validation policy applies.
