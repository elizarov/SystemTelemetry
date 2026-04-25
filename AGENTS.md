## Documentation

- Keep `docs/specifications.md` in sync with behavior changes before finishing work.
- Keep `docs/diagnostics.md` in sync with diagnostics-flow and trace/dump/screenshot behavior changes before finishing work.
- Keep `docs/architecture.md` in sync with structural/code-organization changes before finishing work.
- Keep `docs/project.md` in sync with project-environment, build/setup, and engineering-constraint changes before finishing work.
- Keep `docs/profile_benchmark.md` up to date after benchmark or profiling experiments by recording the latest baseline, hotspots, tested hypotheses, and whether each experiment helped or regressed.
- Keep a single source of truth for every example, format description, and configuration-language reference; when one file is designated as the maintained source, update other docs to refer to it instead of duplicating the same example or format details.
- Document new requirements briefly in the same style as the existing spec.
- Keep docs/specs in present-tense steady-state language; describe only the intended behavior and structure, not transitions, historical notes, removals, or migration context.
- After changing diagnostics behavior, update `docs/diagnostics.md` instead of restating those rules here.
- Write miniaml commit message. One line, just action that was done starting the verb Added/Chaged/Refactored/Removed/etc and a very short summarization of the change. Focus on code/logic changes, only mention docs work if that was the only explicit task.

## Build And Validation

- Always use `build.cmd` for builds.
- Run `build.cmd` outside the sandbox; CMake incremental generation/builds need file deletes, renames, and timestamp updates in `build\` that the sandbox denies with `Access is denied`.
- Keep all build artifacts and temporary compiler files in `build\` so the repository root stays clean.
- Keep the repo-level `.clang-format` in sync with the dominant non-vendored C++ style, especially the non-aligned wrapped-parameter and wrapped-argument layout used across the project.
- Use the top-level `format.cmd` entry point for C++ formatting checks and fixes; `format` checks non-vendored project sources and `format fix` applies the repo style to those files.
- Do not run build steps and validation runs in parallel; always finish the build first, then run validation commands sequentially against the freshly built artifacts.

Validation workflow:

- Build first with `build.cmd`.
- Prefer headless verification commands after the build so checks are repeatable and do not depend on manually closing the UI.
- Use the smallest `/exit` combination that proves the change.
- When headless validation is meant to exercise the built-in `resources/config.ini` behavior, always add `/default-config` so the executable-side `config.ini` overlay does not mask the embedded defaults.
- When validation commands specify diagnostics paths explicitly, point them somewhere under `build\` so trace, dump, screenshot, and fake files do not pollute the repository root.
- Use [docs/diagnostics.md](docs/diagnostics.md) as the single maintained source of truth for post-build diagnostics command examples and what to inspect.

## Project Constraints

- Ignore the stray `$null` file at the repository root when it appears; it is a Codex sandbox artifact, not a project file.
- This project has a single deployment target; do not preserve backwards compatibility for legacy configs unless the user explicitly asks for it.
- Keep headers declarative: non-template and non-inline-required production logic belongs in `.cpp` files, not in project headers.
- If you trip over a project-specific pitfall and then resolve it, add a short note here for future work so the mistake is less likely to repeat.

## Pitfall Notes

- Keep fake-runtime startup failures aligned with the diagnostics dialog policy; a direct modal dialog in `/fake /exit` can look like `/exit` is broken because the headless process waits behind it.
- Win32 dialog templates and control ids live in `resources/SystemTelemetry.rc` and `resources/resource.h`; when a shell dialog layout or control placement looks wrong, check those files before tracing through `src/dashboard_shell_ui.cpp`.
- If rebuilt defaults seem unchanged, check the executable-side `config.ini` first; it overlays the embedded `resources/config.ini` template and `Save Config` preserves that live file.
- If embedded `config.ini` or `localization.ini` edits seem ignored after an incremental build, keep `resources/SystemTelemetry.rc` wired to those payload files through explicit CMake dependencies so the resource object rebuilds.
- When restoring saved placement across monitors with different DPI scales, do not pre-scale the destination window size before the move; let `WM_DPICHANGED` apply the monitor transition first or the bounds can be double-scaled.
- Login startup and monitor hotplug can race ahead of monitor enumeration; when `display.monitor_name` is configured, keep a placement watch armed until the target display becomes enumerable instead of locking in a fallback monitor.
- Gigabyte SIV assembly loading may temporarily need the SIV install directory as the process current directory, but always restore the original launch working directory afterward so diagnostics paths and Save dialogs keep using the startup folder.
- `profile_benchmark.cmd` supports `/daemon-start`; start that elevated helper once when unattended benchmark runs need to be triggered repeatedly from the sandbox, then let ordinary benchmark invocations queue through the daemon so each request rebuilds in the elevated console before profiling without prompting for UAC each time.
- When a benchmark optimization experiment fails or regresses, record that hypothesis and the observed benchmark result in `docs/profile_benchmark.md` before finishing so future work can avoid repeating it blindly.
- If `devenv.cmd` changes between Visual Studio toolchains, delete `build\cmake` before the next `build.cmd` run so CMake, MSVC, and the vcpkg-detected compiler do not mix mismatched compiler and STL versions.
- Git pathspecs such as `tests/**/*.cpp` do not cover top-level files like `tests/benchmarks.cpp`; formatter and hook discovery should start from broad `*.cpp` and `*.h` pathspecs, then apply the repo eligibility filter.
