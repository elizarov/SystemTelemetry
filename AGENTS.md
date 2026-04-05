Keep `docs/specifications.txt` in sync with behavior changes before finishing work.
Keep `docs/diagnostics.txt` in sync with diagnostics-flow and trace/dump/screenshot behavior changes before finishing work.
Keep `docs/architecture.txt` in sync with structural/code-organization changes before finishing work.
Keep `docs/project.txt` in sync with project-environment, build/setup, and engineering-constraint changes before finishing work.
Keep a single source of truth for every example, format description, and configuration-language reference; when one file is designated as the maintained source, update other docs to refer to it instead of duplicating the same example or format details.
Document new requirements briefly in the same style as the existing spec.
Always use `build.cmd` for builds.
Run `build.cmd` outside the sandbox; CMake incremental generation/builds need file deletes, renames, and timestamp updates in `build\` that the sandbox denies with `Access is denied`.
Keep all build artifacts and temporary compiler files in `build\` so the repository root stays clean.
Ignore the stray `$null` file at the repository root when it appears; it is a Codex sandbox artifact, not a project file.
This project has a single deployment target; do not preserve backwards compatibility for legacy configs unless the user explicitly asks for it.
Do not run build steps and validation runs in parallel; always finish the build first, then run validation commands sequentially against the freshly built artifacts.
If you trip over a project-specific pitfall and then resolve it, add a short note here for future work so the mistake is less likely to repeat.
Pitfall note: keep fake-runtime startup failures aligned with the diagnostics dialog policy; a direct modal dialog in `/fake /exit` can look like `/exit` is broken because the headless process waits behind it.
Pitfall note: if rebuilt defaults seem unchanged, check the executable-side `config.ini` first; it overlays the embedded `resources/config.ini` template and `Save Config` preserves that live file.
Pitfall note: if `build.cmd` fails in sandbox with CMake/MSBuild `Access is denied` errors on stamp, tmp, or tlog files, rerun it outside the sandbox; incremental CMake builds require mutable build-tree file operations that the sandbox blocks.

After changing diagnostics behavior, update `docs/diagnostics.txt` instead of restating those rules here.

Validation workflow:
- Build first with `build.cmd`.
- Prefer headless verification commands after the build so checks are repeatable and do not depend on manually closing the UI.
- Use the smallest `/exit` combination that proves the change.

Post-build verification examples:
- Trace format/path check: `build\\SystemTelemetry.exe /trace /exit`
- Dump content check: `build\\SystemTelemetry.exe /dump /exit`
- Screenshot/exported rendering check: `build\\SystemTelemetry.exe /screenshot /exit`
- Full diagnostics pass: `build\\SystemTelemetry.exe /trace /dump /screenshot /exit`
- Repeatable UI snapshot check from fake data: `build\\SystemTelemetry.exe /fake /screenshot /exit`
- Repeatable full fake-data pass: `build\\SystemTelemetry.exe /fake /trace /dump /screenshot /exit`

What to inspect:
- UI/rendering changes: confirm `telemetry_screenshot.png` matches the intended layout and no text is clipped.
- Sensor/telemetry changes: confirm `telemetry_dump.txt` and `telemetry_trace.txt` show the expected values, provider diagnostics, and failure paths.
- Diagnostics-flow changes: confirm the requested files are produced, overwritten/appended as expected for the chosen mode, fake-data reloads come from `telemetry_fake.txt` when requested, and the `/exit` run terminates promptly.
