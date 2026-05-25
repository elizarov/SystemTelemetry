# Diagnostics Package

`src/diagnostics/` owns no-window diagnostics command-line parsing, one-shot export orchestration, requested output exports, trace-oriented failure reporting, snapshot dump I/O, app icon PNG export, and native crash reports.

## Responsibilities

- Parse diagnostics switches and choose one-shot or UI-attached diagnostics behavior.
- Report which command-line arguments diagnostics parsing consumes so console entry points can reject unsupported or malformed leftovers without duplicating the diagnostics switch table.
- Manage requested trace, snapshot dump, screenshot, app icon, minimal config overlay, and full config exports.
- Expose explicit output-handler and config-extension boundaries for layout guide sheet export without depending on the `layout_guide_sheet` package.
- Create and pass the top-level trace session when trace is enabled.
- Route user-facing diagnostics errors through the caller-provided handler while keeping trace-owned failure records in diagnostics.
- Serialize and parse snapshot dumps shared by diagnostics export and fake-runtime import.
- Install the crash report handler from the application entry point after diagnostics options are parsed.
- Export runtime-rendered app icons through the same programmatic icon geometry used by the live window, tray, and dialogs.

## Boundaries

- Diagnostics orchestrates exports but uses live runtime state and the shared renderer scene instead of a separate rendering implementation.
- Diagnostics does not own HWND prompts, menu items, message boxes, or shell dialogs; dashboard and main own user-visible UI reporting.
- Headless owns console `stderr` reporting; diagnostics supplies the callback seam and never links UI reporting code.
- Renderer owns generated pixel-buffer PNG encoding; diagnostics may delegate app icon bitmap writes to renderer instead of owning PNG compression details.
- `src/util/trace.*` owns trace line emission plus compact shared trace value formatting helpers.
- Layout guide sheet planning, config extension loading, color resolution, and rendering live outside diagnostics; `headless` supplies those callbacks and diagnostics owns option parsing, validation, output paths, and trace records.
- Diagnostics behavior and validation recipes are specified in [docs/diagnostics.md](../diagnostics.md).
