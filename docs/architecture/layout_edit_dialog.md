# Layout Edit Dialog Package

`src/layout_edit_dialog/` owns the modeless `Edit Configuration` window boundary, config-tree selection, right-pane editing, preview and revert flow, and theme preview drawing.

## Responsibilities

- Build the modeless editor window and route UI events into descriptor-backed editor handlers.
- Refresh the config tree directly from current live config through the shared uncached tree-model builder.
- Preview and revert edits through the same config mutation path used by drag-based layout-edit interaction.
- Keep focused helper modules under `src/layout_edit_dialog/impl/` when they are private to the dialog package.
- Reuse config color math for RGB/LCH/HSV conversion instead of carrying dialog-local color-space logic.
- Reuse the config-owned metric-to-board-binding resolver and runtime active-binding state for metric `Binding` rows, including provider fallback metrics.
- Own construction and drawing of the theme selector's color-mix triangle in `theme_preview.*` so dialog painting and benchmark coverage use the same implementation.

## Boundaries

- `layout_edit_dialog` may depend on `layout_edit_dialog`, `config`, `layout_edit`, `layout_model`, `telemetry`, `util`, and `widget`.
- It does not depend on dashboard renderer, diagnostics, display, main, renderer implementation, or Direct2D/DirectWrite/WIC headers.

## Implementation Notes

- Config tree item `lParam` values point into `LayoutEditDialogState::visibleTreeModel`; rebuilds keep the old visible model alive until old tree-view items are deleted, then insert items from the new visible model with selection notifications suppressed.
