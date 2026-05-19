# Layout Model Package

`src/layout_model/` owns renderer-safe layout-edit contracts and shared behavior used by dashboard rendering, layout-edit mode, and the modeless editor.

## Responsibilities

- Define edit-target identity, active-region payloads, artifact matching, hit-priority behavior, and shared anchor-shape drawing helpers.
- Own read-only layout-node parameter metadata and guide-weight preview helpers.
- Share dashboard overlay state contracts without depending on dashboard shell or renderer implementation.
- Resolve focus and selection helpers, anchor subject extraction, and edit-artifact ordering policy.

## Boundaries

- `layout_model` may depend on `layout_model`, `config`, `renderer`, `util`, and `widget`.
- It does not depend on dashboard, dashboard renderer, diagnostics, display, layout-edit interaction, layout-edit dialog, main, telemetry, or graphics implementation packages.
- Types in this package stay DTO-like and renderer-safe so they can cross package boundaries cleanly.
