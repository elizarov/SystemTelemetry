# Dashboard Renderer Package

`src/dashboard_renderer/` owns dashboard scene traversal, static layout resolution, renderer style input selection, drawing-mode state, widget-host services, and active-region collection.

## Responsibilities

- Convert the active config into resolved dashboard, card, widget, guide, anchor, and dynamic edit-artifact geometry.
- Implement `WidgetHost` for widget drawing and layout-edit artifact registration.
- Own the registered artifact storage, shared affordance presets, and low-level anchor-region construction used by layout editing.
- Own the fixed-slot metric definition and sample-text lookup cache in `impl/metric_lookup_cache.*`.
- Build live dashboard frames as an ordered snapshot bitmap, optional overlay bitmap, and per-layer widget animation lists. `DashboardOverlayState::ShouldDrawOverlayLayer()` is the renderer gate that skips overlay bitmap construction when no overlay content is visible.
- Own the live presentation handoff in `impl/render_thread.*`; widgets publish stable animation keys and opaque target states through `WidgetHost`, the render thread owns `DashboardAnimationTimeline`, deterministic offscreen rendering bypasses interpolation, and benchmark immediate-present mode runs the same composition steps synchronously.
- Split layout-edit overlay drawing into background affordances, dragged overlay content, and foreground affordances so fixed-content highlights can remain behind active dragged rows or children while dragged-content highlights stay above them.
- Tag resolved widget layouts and registered edit artifacts with layout-edit overlay owners, then derive overlay sublayers from those tags during active row or child drags instead of classifying affordances by rendered rectangles.
- Keep snapshot and overlay widget animation lists separate while resolving both lists through the same render-thread keyed timeline, so layout edits can move widget content between layers without restarting data animation.
- Keep widget animation sample types, transition details, and render geometry payloads out of the dashboard-renderer boundary; those details stay package-private under `widget`.
- Render selected and hovered layout-edit highlights, layout and widget guides, gap anchors, size-similarity indicators, dotted outlines, and dragged container-child replay.
- Produce copied `LayoutEditActiveRegions` snapshots for live layout editing and diagnostics screenshot validation.
- Supply resolved-card summaries, card-chrome artifact hooks, and rendering hooks needed by the layout-guide-sheet package.

## Boundaries

- `dashboard_renderer` may depend on `dashboard_renderer`, `config`, `layout_model`, `renderer`, `telemetry`, `util`, and `widget`.
- It does not depend on dashboard shell, diagnostics orchestration, display, layout-edit interaction, layout-edit dialog, main, or Direct2D/DirectWrite/WIC implementation headers.
- Graphics-backend details remain encapsulated in `renderer`.
