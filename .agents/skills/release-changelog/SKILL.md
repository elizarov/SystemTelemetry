---
name: release-changelog
description: Create and refresh CaseDash release changelog entries in docs/changelog.md. Use when preparing release notes, summarizing user-visible changes since the latest Git tag, creating a top changelog chunk, or checking the machine-readable changelog format before release.
---

# Release Changelog

## Workflow

1. Run the source report helper from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\release_changelog_report.ps1
```

The helper writes `build\release_changelog_report.md` with the latest reachable tag, commit subjects, changed files, and per-commit stats.

2. Read the report and summarize only user-visible changes. Include runtime behavior, diagnostics, installer behavior, website/download behavior, hardware support, documentation that users rely on, release packaging that users touch, and performance or binary-size optimization. Mention performance and size work only briefly in summary form, avoiding benchmark minutiae or implementation details. Omit pure refactors, tests, internal tooling, and wording-only docs changes unless they change what users or release consumers see.

3. Update `docs\changelog.md` at the top of the file. Keep one concise `- ` bullet per release note. Prefer direct release-note verbs such as `Added`, `Improved`, `Fixed`, `Refined`, or `Moved`.

4. Preserve older chunks exactly below the top chunk, separated by a line containing exactly `---`.

## Changelog Format

Use this format before `release.cmd` stamps the release version:

```markdown
- Added ...
- Improved ...

---
## v0.1

- Added ...
```

Rules:

- The current release draft must be the first chunk in `docs\changelog.md`.
- Release chunks are separated only by a line containing exactly `---`.
- The draft chunk normally has no heading. If the top chunk already starts with `## v<version>`, treat it as a stamped release chunk.
- When adding a new draft above an already stamped release, put the new bullets first, then `---`, then the older release chunk.
- Do not add a file title, date heading, category headings, or nested bullets unless the release procedure is deliberately changed.

`release.cmd` owns adding or validating the top `## v<VERSION>` header. The GitHub Release workflow owns publishing the body of that stamped top chunk.
