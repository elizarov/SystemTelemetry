## Documentation

- Keep `docs/specifications.md` in sync with behavior changes before finishing work.
- Keep `docs/diagnostics.md` in sync with diagnostics-flow and trace/dump/screenshot behavior changes before finishing work.
- Keep `docs/architecture.md` in sync with structural/code-organization changes before finishing work.
- Keep `docs/project.md` in sync with project-environment, build/setup, and engineering-constraint changes before finishing work.
- Keep a single source of truth for every example, format description, and configuration-language reference; when one file is designated as the maintained source, update other docs to refer to it instead of duplicating the same example or format details.
- Document new requirements briefly in the same style as the existing spec.
- Keep docs/specs in present-tense steady-state language; describe only the intended behavior and structure, not transitions, historical notes, removals, or migration context.
- After changing diagnostics behavior, update `docs/diagnostics.md` instead of restating those rules here.

## Build And Validation

- Always use `build.cmd` for builds.
- Run `build.cmd` outside the sandbox; CMake incremental generation/builds need file deletes, renames, and timestamp updates in `build\` that the sandbox denies with `Access is denied`.
- Keep all build artifacts and temporary compiler files in `build\` so the repository root stays clean.
- Do not run build steps and validation runs in parallel; always finish the build first, then run validation commands sequentially against the freshly built artifacts.

Validation workflow:

- Build first with `build.cmd`.
- Prefer headless verification commands after the build so checks are repeatable and do not depend on manually closing the UI.
- Use the smallest `/exit` combination that proves the change.
- Use [docs/diagnostics.md](docs/diagnostics.md) as the single maintained source of truth for post-build diagnostics command examples and what to inspect.

## Project Constraints

- Ignore the stray `$null` file at the repository root when it appears; it is a Codex sandbox artifact, not a project file.
- This project has a single deployment target; do not preserve backwards compatibility for legacy configs unless the user explicitly asks for it.
- If you trip over a project-specific pitfall and then resolve it, add a short note here for future work so the mistake is less likely to repeat.

## Pitfall Notes

- Keep fake-runtime startup failures aligned with the diagnostics dialog policy; a direct modal dialog in `/fake /exit` can look like `/exit` is broken because the headless process waits behind it.
- If rebuilt defaults seem unchanged, check the executable-side `config.ini` first; it overlays the embedded `resources/config.ini` template and `Save Config` preserves that live file.
- When restoring saved placement across monitors with different DPI scales, do not pre-scale the destination window size before the move; let `WM_DPICHANGED` apply the monitor transition first or the bounds can be double-scaled.
- Login startup and monitor hotplug can race ahead of monitor enumeration; when `display.monitor_name` is configured, keep a placement watch armed until the target display becomes enumerable instead of locking in a fallback monitor.
