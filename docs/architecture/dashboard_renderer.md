# Dashboard Renderer Package

`src/dashboard_renderer/` owns dashboard scene traversal, static layout resolution, renderer style input selection, drawing-mode state, widget-host services, and active-region collection.

## Responsibilities

- Convert the active config into resolved dashboard, card, widget, guide, anchor, and dynamic edit-artifact geometry.
- Implement `WidgetHost` for widget drawing and layout-edit artifact registration.
- Own the registered artifact storage, shared affordance presets, and low-level anchor-region construction used by layout editing.
- Render selected and hovered layout-edit highlights, layout and widget guides, gap anchors, size-similarity indicators, dotted outlines, and dragged container-child replay.
- Produce copied `LayoutEditActiveRegions` snapshots for live layout editing and diagnostics screenshot validation.
- Supply resolved-card summaries, card-chrome artifact hooks, and rendering hooks needed by the layout-guide-sheet package.

## Boundaries

- `dashboard_renderer` may depend on `dashboard_renderer`, `config`, `layout_model`, `renderer`, `telemetry`, `util`, and `widget`.
- It does not depend on dashboard shell, diagnostics orchestration, display, layout-edit interaction, layout-edit dialog, main, or Direct2D/DirectWrite/WIC implementation headers.
- Graphics-backend details remain encapsulated in `renderer`.
