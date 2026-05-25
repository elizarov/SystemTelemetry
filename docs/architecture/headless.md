# Headless Package

`src/headless/` owns the non-shipped `CaseDashHeadless.exe` entry point and the layout guide sheet diagnostics output adapter.

## Responsibilities

- Parse the same diagnostics command-line surface through `diagnostics` and force one-shot behavior as if `/exit` were supplied.
- Own strict console-only command-line shape validation and usage help for `CaseDashHeadless.exe` by rejecting arguments left unconsumed by diagnostics parsing, keeping that text out of the shipped dashboard executable.
- Install crash reporting and run diagnostics export orchestration without creating dashboard windows, menus, dialogs, tray icons, or service/elevation maintenance modes.
- Build as an explicit console-subsystem executable and report validation or runtime diagnostics failures to `stderr`, with trace records still written when `/trace` is active.
- Provide the only production-code adapter that connects diagnostics `/layout-guide-sheet` output to the public `layout_guide_sheet` package API.
- Link and load the generated `[layout_guide_sheet]` config resource, then provide layout guide sheet color resolution as a diagnostics config extension.
- Report validation failures through command exit codes, stderr, and trace output rather than modal UI.
- Support website and CI-generated assets such as themed screenshots, app icons, and layout guide sheets.

## Boundaries

- `headless` may depend on `diagnostics`, `layout_guide_sheet`, and `util`.
- `headless` must not depend on `dashboard`, `layout_edit_dialog`, `main`, or `display`.
- `headless` includes only the public `layout_guide_sheet/layout_guide_sheet.h` boundary and never package-private `layout_guide_sheet/impl/*` headers.
- Reusable diagnostics, renderer, telemetry, layout-edit model, and widget behavior stays in the owning no-window packages instead of being copied into `headless`.
