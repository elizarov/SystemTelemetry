Keep `docs/specifications.txt` in sync with behavior changes before finishing work.
Keep `docs/diagnostics.txt` in sync with diagnostics-flow and trace/dump/screenshot behavior changes before finishing work.
Keep `docs/architecture.txt` in sync with structural/code-organization changes before finishing work.
Keep `docs/project.txt` in sync with project-environment, build/setup, and engineering-constraint changes before finishing work.
Document new requirements briefly in the same style as the existing spec.
Always use `build.cmd` for builds.
Keep all build artifacts and temporary compiler files in `build\` so the repository root stays clean.
Ignore the stray `$null` file at the repository root when it appears; it is a Codex sandbox artifact, not a project file.

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

What to inspect:
- UI/rendering changes: confirm `telemetry_screenshot.png` matches the intended layout and no text is clipped.
- Sensor/telemetry changes: confirm `telemetry_dump.txt` and `telemetry_trace.txt` show the expected values, provider diagnostics, and failure paths.
- Diagnostics-flow changes: confirm the requested files are produced, overwritten/appended as expected for the chosen mode, and the `/exit` run terminates promptly.
