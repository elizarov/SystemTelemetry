# Diagnostics Package

`src/diagnostics/` owns diagnostics command-line parsing, headless `/exit` orchestration, requested output exports, trace-oriented failure reporting, snapshot dump I/O, app-icon PNG export, and native crash reports.

## Responsibilities

- Parse diagnostics switches and choose headless or UI-attached diagnostics behavior.
- Manage requested trace, dump, screenshot, layout-guide-sheet, app-icon, minimal-config, and full-config exports.
- Create and pass the top-level trace session when trace is enabled.
- Prefer trace logging plus failure exit codes over blocking modal behavior for diagnostics failures.
- Serialize and parse snapshot dumps shared by diagnostics export and fake-runtime import.
- Install the crash report handler from the application entry point after diagnostics options are parsed.
- Export runtime-rendered app icons through the same programmatic icon geometry used by the live window, tray, and dialogs.

## Boundaries

- Diagnostics orchestrates exports but uses live runtime state and the shared renderer scene instead of a separate rendering implementation.
- `src/util/trace.*` owns trace line emission plus generic trace value formatting and quoting helpers.
- Layout-guide-sheet planning and rendering live in `layout_guide_sheet`; diagnostics owns the export command and trace records.
- Diagnostics behavior and validation recipes are specified in [docs/diagnostics.md](../diagnostics.md).
