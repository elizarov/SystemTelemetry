# Layout Edit Package

`src/layout_edit/` owns live layout-edit interaction, active-region hit testing, drag flow, tooltip interpretation, config mutation helpers, and guide and reorder config helpers.

## Responsibilities

- `LayoutEditController` owns hover state, active drags, capture, cursor choice, and drag-session flow.
- Resolve actionable targets from renderer-produced `LayoutEditActiveRegions` snapshots.
- Discover snap candidates and drag targets inside package-private interaction helpers under `src/layout_edit/impl/`.
- Interpret tooltip payloads and format tooltip text for live UI and diagnostics hover validation.
- Mutate layout-node config through `layout_edit_service.*`, resolving `{editCardId,nodePath}` through shared helpers.
- Mirror dashboard-layout edits into the active named layout when the edit targets the live dashboard layout.
- Declare widget layout-node edit descriptors in `layout_edit_target_descriptor.*` so tree labels, editor kind, title, hint, tooltip description, trace identity, and value format have one owner.

## Boundaries

- `layout_edit` may depend on `layout_edit`, `config`, `layout_model`, `util`, and `widget`.
- It consumes active-region snapshots produced by `dashboard_renderer` without depending on that package.
- It does not own modeless editor controls; those live in `layout_edit_dialog`.
