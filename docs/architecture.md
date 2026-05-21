# CaseDash Architecture

This document routes code-organization, subsystem-boundary, and build-graph notes. Source code is the implementation authority, and `lint.cmd` is the maintained architecture-check entrypoint.
See also: [docs/specifications.md](specifications.md) for product behavior, [docs/diagnostics.md](diagnostics.md) for diagnostics contracts, [docs/layout.md](layout.md) for config language, [docs/build.md](build.md) for developer commands, and the package notes under [docs/architecture/](architecture/).

## Top-Level Packages

- [config](architecture/config.md) - persisted configuration model, INI parsing and writing, schema metadata, theme and color resolution, config color text formatting, and config-facing contracts.
- [dashboard](architecture/dashboard.md) - shell UI, controller orchestration, tray integration, menus and menu labels, auto-start, service registration, and user-facing command flow.
- [dashboard_renderer](architecture/dashboard_renderer.md) - dashboard scene traversal, layout resolution, widget host services, drawing-mode state, and active-region collection.
- [diagnostics](architecture/diagnostics.md) - diagnostics CLI parsing, headless runs, trace-owned exports, snapshot dumps, app icon export, and native crash reports.
- [display](architecture/display.md) - monitor enumeration, display targeting, placement, scale, and wallpaper/configure-display helpers.
- [layout_edit](architecture/layout_edit.md) - live layout-edit interaction, hit testing, drag flow, config mutation helpers, tooltip text, and active-region trace output.
- [layout_edit_dialog](architecture/layout_edit_dialog.md) - modeless configuration editor window, editor controls, preview/revert flow, and theme preview drawing.
- [layout_guide_sheet](architecture/layout_guide_sheet.md) - diagnostics layout guide sheet planning, representative-card selection, callout layout, leader routing, and sheet rendering.
- [layout_model](architecture/layout_model.md) - renderer-safe layout-edit contracts, edit-target identity, active-region behavior, hit priority, and overlay state.
- [main](architecture/main.md) - process entry point, command-line startup mode selection, elevation handoff, service host entry, and runtime config loading.
- [renderer](architecture/renderer.md) - D2D-free renderer interface, render-space DTOs, style resources, Direct2D/DirectWrite/WIC backend, text measurement, and bitmap export support.
- [telemetry](architecture/telemetry.md) - telemetry runtime, snapshot contracts, metric catalog, provider bridges, FPS service protocol, fake runtime, and retained histories.
- [util](architecture/util.md) - domain-neutral file path, command-line, string, enum, UTF-8, resource, localization, numeric formatting, Win32 error formatting, trace, and callback helpers.
- [vendor](architecture/vendor.md) - narrow vendored source kept outside project layering rules where package-managed dependencies are not practical.
- [widget](architecture/widget.md) - widget contracts, widget factories, widget-local drawing/layout behavior, edit-artifact registration, and app icon and card-chrome geometry.

Other top-level areas:

- `resources/` contains the resource script, source config and localization files for the generated text atlas, dialog templates, manifest, and fallback executable icon; build-generated trace string catalog text joins that same atlas.
- `tests/` contains unit tests for config, layout resolution, retained-history behavior, and the native benchmark host.
- `src/tools/` contains native repository tools built into `build\CaseDashTools.exe`, including the maintained combined lint scanner behind `lint.cmd`. `tools/` contains script entrypoints for formatting, profiling, generated assets, and compatibility helpers, while `tools/lint_config.json` owns the lint policy data.
- `.agents/skills/` contains reusable agent or automation skills.
- `web/` contains the static website source, browser-side theme switching code, CSS, and the website build script that generates `web/dist/`.
- `.github/workflows/` contains runner-hosted build, test, format, lint, unused-include, package, release, and website deployment automation.

## Layered Core

- Dependencies flow downward. Higher layers may include lower-layer contracts, but lower layers do not include or call higher layers.
- The core layer order is `util` -> `config` -> `renderer` and `telemetry` -> `widget` -> `layout_model` -> application-facing packages such as `dashboard`, `dashboard_renderer`, `diagnostics`, `display`, `layout_edit`, `layout_edit_dialog`, and `main`.
- `util` is the lowest layer and is available to every non-util package for domain-neutral helpers.
- Cross-layer shared types belong in the lowest layer that semantically owns them. Config-language DTOs live in `config`, runtime telemetry DTOs live in `telemetry`, and domain-neutral helpers live in `util`.
- Custom hash-based containers or caches that replace `std::unordered_map` live in a dedicated named `.h`/`.cpp` module under the owning package or its `impl` directory. Feature providers, renderers, and controllers use those modules through a small API instead of embedding hashing, probing, or collision handling locally.
- Public cross-thread contracts document thread affinity, callback thread, blocking behavior, and ownership or lifetime guarantees in the declaring header before the relevant method or callback.
- Maintained source and tests do not use conditional-compilation guards. Code must always compile for every native target; target-specific benchmark or diagnostics helpers stay ordinary functions and rely on the linker to remove unused code from targets that do not reference them.
- `lint.cmd` runs the combined lint scanner and enforces package dependencies, package-private implementation boundaries, header-body rules, include-path rules, local `NOLINT` policy, source-policy bans, and the renderer-only Direct2D boundary before reporting success.

## Package Dependency Rules

- `lint.cmd` is the maintained checker for package dependency rules and package-private boundaries.
- Package-specific notes under [docs/architecture/](architecture/) list the allowed dependency shape for each package.
- Application-facing packages such as `dashboard`, `diagnostics`, `display`, and `main` may compose lower-layer services but do not move reusable lower-layer logic upward into shell code.
- Files below package subdirectories are package-private implementation modules. Dependencies from a different top-level package into modules such as `widget/impl/*`, `telemetry/board/*`, or `dashboard_renderer/impl/*` fail the source dependency check.

## Runtime Flows

- Startup begins in `main`, parses command-line options, handles service and elevation modes, loads config from the embedded template plus executable-side overlay, then chooses the normal UI path or the diagnostics `/exit` path.
- Telemetry collects through `telemetry`, publishes copied snapshots and resolved runtime selections, and supplies the metric data consumed by renderer-facing flows.
- Rendering flows through `dashboard_renderer`, which combines the active config, latest telemetry snapshot, renderer style input, and widget draw contracts into live paints and diagnostics screenshot exports.
- Layout-edit interaction starts from shell pointer events, resolves targets from renderer-produced active regions, mutates config through `layout_edit`, and shares preview behavior with `layout_edit_dialog`.
- Diagnostics owns requested trace, snapshot dump, screenshot, layout guide sheet, app icon, and config exports, using the same runtime state and render paths as the live dashboard.
- Persistence compares minimal saves against the loaded INI text, uses the embedded template for full exports, and relaunches through maintained elevation helpers when target files or registry/service state require elevation.

## Resources And Build Graph

- `resources/CaseDash.rc` owns dialogs and icons; CMake generates the compressed embedded text atlas resource from config, localization, and the deduplicated first source-use `RES_STR` trace string catalog with collision-checked hash ids.
- `resources/resource.h` owns resource and control ids used by shell and dialog code.
- `CMakeLists.txt` is the single native build graph for the app, tests, benchmarks, resources, and mixed-mode board-provider bridge object libraries.
- CMake reads `VERSION` and Git metadata during configure, then generates build metadata headers and target-specific manifest resource scripts.
- The native app target links shell, controller, config, telemetry, renderer, diagnostics, widget, and layout-edit subsystems into one Win32 executable.
- `.github/workflows/validation.yml` checks formatting through `format.cmd`, builds through `build.cmd`, runs tests through `test.cmd`, packages the MSI through `package.cmd`, and runs the unused-include sweep through `lint.cmd includes` on the Windows runner.
- `build.cmd` keeps the manifest-installed dependency tree in repo-root `vcpkg\`, while vcpkg download archives and registry clones live under the shared cache root.

## Source Dependency Checks

- Each graph node represents a source module, where a matching `.h` and `.cpp` pair share one node named by the extensionless path under `src`.
- The native lint tool builds the dependency graph in memory for package dependency rules, package-private boundary checks, DAG topological package order, package LOC summaries, lower-level dependencies, and non-vendored files above the configured LOC threshold.
- The graph includes a synthetic `d2d` package for Direct2D, DirectWrite, WIC, and WRL includes so every package except `renderer` rejects accidental graphics-stack coupling.
