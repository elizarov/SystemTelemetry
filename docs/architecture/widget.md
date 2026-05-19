# Widget Package

`src/widget/` owns widget contracts, widget-local layout and drawing behavior, widget factories, widget-facing layout-edit DTOs, shared card-chrome geometry, and programmatic app icon geometry.

## Responsibilities

- `widget.*` owns the widget interface plus enum-backed and special widget factories.
- `widget_host.h` defines the D2D-free host boundary consumed by widgets.
- `animation_types.*` defines public animation identity through stable data keys.
- `animation.*` defines the public opaque animation interfaces used to draw sampled widget states on a regular `Renderer`.
- `impl/animation_primitives.*` keeps scalar and throughput sample state, interpolation, transition, and draw-data details package-private.
- Widget implementations under `src/widget/impl/` own concrete draw behavior, preferred-size logic, layout-state modules, and layout-edit artifact registration.
- `card_chrome_layout.*` owns shared card-chrome geometry used by layout resolution and the special card-chrome widget.
- `app_icon_geometry.*` draws the shared CaseDash icon mark from resolved theme colors for live shell icons and diagnostics PNG export.
- Widgets call drawing and text operations through the renderer reference exposed by `WidgetHost`.
- Widgets call animation through `WidgetHost::AddWidgetAnimation()` after resolving metric data into widget-private target state. The dashboard renderer assigns the submitted command to the active snapshot or overlay collection pass, and the submitted `WidgetAnimation` carries only the key, dirty geometry, and draw routine.
- Widgets draw base dashboard content through `Widget::Draw()` and overlay-only content through `Widget::DrawOverlay()`. Metric-list row drag replay uses the overlay hook so its static row content and animations move together above the snapshot layer.
- Widget draw modules refer to colors by render color id; renderer owns resolved RGBA values.

## Boundaries

- `widget` may depend on `widget`, `renderer`, `telemetry`, `config`, and `util`.
- It does not depend on dashboard, diagnostics, display, layout-edit, main, Direct2D, DirectWrite, WIC, or WRL implementation headers.
- Widget-facing edit-artifact DTO contracts live here; interaction semantics live in `layout_edit`, and renderer-produced active-region snapshots use `layout_model`.
