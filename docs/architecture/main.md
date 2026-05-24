# Main Package

`src/main/` owns process entry, startup mode selection, runtime config I/O wiring, service host entry, elevation handoff, and main-process constants.

## Responsibilities

- Enter the single-executable service host when launched with `/service`.
- Initialize process-wide shell settings for normal dashboard startup.
- Parse command-line options early enough to install crash reporting and choose startup mode.
- Run with the executable manifest's UTF-8 active code page so Win32 A API boundaries consume the app's default narrow text encoding.
- Relay `/elevate` runs to an elevated child process while preserving diagnostics arguments and current working directory.
- Choose either the normal UI path or the production one-shot diagnostics path.
- Reject `/layout-guide-sheet` with a clear validation error because that output belongs to `CaseDashHeadless.exe`.
- Start config load from embedded `resources/config.ini`, apply the executable-side overlay unless suppressed, and resolve active layout plus runtime selections before telemetry and rendering start.
- Keep the executable manifest declaring UTF-8 as the active code page and disabling file virtualization so config reads and writes target the executable-side location.

## Boundaries

- `main` composes application-facing packages but does not own reusable dashboard, diagnostics, telemetry, renderer, or config logic.
- Generated build metadata enters through `build_version.h.in` and the CMake-generated header.
