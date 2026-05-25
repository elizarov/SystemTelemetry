# CaseDash Layout-Edit Mode

This document owns user-visible layout-edit mode, edit-target interaction, modeless editor behavior, and layout-edit save or discard behavior.
See also: [docs/specifications.md](specifications.md) for general product behavior, [docs/layout.md](layout.md) for config language ownership, [docs/layout_guide_sheet.md](layout_guide_sheet.md) for the diagnostics layout guide sheet, and [docs/diagnostics.md](diagnostics.md) for layout-edit diagnostics switches and trace behavior.

## Mode Lifecycle

- `Edit Layout` toggles interactive layout-edit mode from the popup menu and hover titlebar edit-layout button, `Layout Editor...` starts layout-edit mode when needed and opens the `Edit Configuration` window, and the command line can also start the dashboard in that mode for live UI or screenshot diagnostics.
- Layout-edit mode stays active across move mode, corner resize mode, layout changes, scale changes, and runtime network or storage selection changes. It ends only when the user explicitly turns it off or when `Save Config` or `Configure Display` completes successfully.
- Closing the editor window closes only the modeless editor window, clears any tree-selection highlight from the dashboard, and keeps layout-edit mode active.
- Turning off layout-edit mode uses the shared unsaved-session prompt with save, discard, and cancel outcomes.
- Turning off layout-edit mode, exiting the app, or restarting as administrator while the edit session is dirty always gives the user an explicit save-or-discard choice before destructive loss of the edit-session state.

## Live Dashboard Interaction

- While layout-edit mode is active, the renderer shows container guides, a thin single-line outline around the hovered widget or hovered card header content, supported widget-local guides, and matching edit cursors.
- Hovering actionable text, card chrome, bars, metric rows, reorder handles, or widget-local geometry exposes the matching highlight and edit affordance for that target class.
- Hovering actionable text or pill bar targets draws the dotted outline around that text or pill bar target regardless of which overlapping handle, wedge, color region, or context action wins the focused edit action.
- While any layout-edit drag is active, hover hit testing and tooltip refresh for other edit targets are paused; the dragged target's active affordance and the dragged widget or card highlight remain visible until release.
- Dashboard corner resize affordances have the same priority as move mode in layout-edit mode: starting resize cancels active edit drags, suppresses layout-edit tooltips, shows the placement overlay while resizing, and returns layout-edit hover behavior after release.
- Drag feedback redraws immediately for each processed pointer move instead of waiting for queued idle paint delivery, so the visible guide, handle, or dragged child tracks the latest mouse position during continuous input.
- When a square text or icon handle overlaps a metric-binding wedge near the same text corner, the square handle wins hover and the wedge stays tucked close to the text corner with a smaller hit area.
- Hovering actionable targets also shows a standard Win32 tooltip whose first line matches the edited config shape and whose second line uses the shared localized description for that target. The dashboard uses the same tooltip engine for titlebar control tooltips and layout-edit tooltips, so moving between the titlebar and dashboard updates the tooltip in place.
- Date/time format wedge tooltips use the inline widget expression for the edited location, such as `[card.time] layout = clock_time(HH:MM)`, so live tooltips, layout guide sheet callouts, and traced hover text share the same first line.
- Spurious mouse-leave notifications that arrive while the pointer is still inside the dashboard do not clear or rebuild layout-edit hover state until the next real pointer movement or explicit hover refresh.
- Once the pointer leaves the dashboard client area, layout-edit tooltips do not reappear from stale hover or color-hit state until the pointer re-enters and produces a new in-client hover target.
- Right-clicking an actionable target prepends one focused `Edit ...` action for that exact target.

## Editor Window

- The modeless `Edit Configuration` window stays separate from the dashboard window, exposes a config-ordered tree plus a live editor pane, previews valid edits immediately, refreshes its tree contents and right-hand editor pane without painting intermediate empty or partially rebuilt content, preserves the tree viewport across rebuilds, keeps only the current edit session inside its save or discard boundary, and reopens at its last user-moved on-screen position.
- When the modeless `Edit Configuration` window opens or syncs to a targeted dashboard item, the matching tree node becomes selected, its editor opens, and the tree scrolls to keep that node visible.
- Card layout groups expose descriptor-backed root widgets such as `metric_list(...)`, `clock_time(...)`, and `clock_date(...)` as selectable editor nodes even when the layout expression has no enclosing row or column container.
- Bringing the `Edit Configuration` window to the foreground also raises the dashboard directly behind it in Z-order so the editor and dashboard stay visually paired, but dashboard-driven refreshes do not raise the editor window unless the user brings it forward.
- Dashboard hit-testing treats the editor as covering the dashboard only when the editor is actually above the dashboard in window Z-order, not merely because the windows overlap on screen or because the editor still has keyboard focus.
- While the editor window is above the dashboard, covered dashboard regions stay mouse-transparent to the editor and suppress dashboard hover, tooltip, and cursor updates for those covered points.
- While the editor window is above the dashboard, selecting a tree node also highlights the matching split guide, widget guide, gap anchor, or text anchor for that config target, and shared gap or ring-stroke targets add the matching widget, card, or dashboard outline.

## Editor Controls

- The editor supports filtering, per-field revert, config-local descriptions, and specialized editors for numeric values, fonts, global font family, theme-token colors, literal or derived color roles, metrics, date/time formats, weight pairs, and metric-list row ordering.
- Literal color editors expose local `RGB`, `LCH`, and `HSV` tabs for editing the same color value, keep alpha below the tabs as a shared channel, show LCH lightness and chroma plus HSV saturation and value rounded to thousandths, show hue values rounded to whole degrees, name the LCH controls `Lightness`, `Chroma`, and `Hue`, show a text-height visual gradient below each RGB, LCH, or HSV slider, and preserve the selected tab while the dialog stays open.
- Date/time format selectors include common 24-hour, 12-hour uppercase and lowercase meridiem, numeric date, month-name date, and weekday date styles while preserving a configured custom value.
- Color target tooltip first lines show the configured color expression rather than the resolved color.
- Edit-dialog dropdowns open above neighboring controls with a popup sized for the available options, capped at ten visible rows.
- Layout sections expose a layout selector dropdown and show the active layout description in the editor pane. Changing the selector updates the active named layout immediately, refreshes the tree to the newly active `[layout.<name>]` section, and keeps layout-edit mode active.
- Theme sections expose a theme selector with an equilateral color-mix preview, show the active theme description in the editor pane, keep their standard tokens expandable as color edits, and `[colors]` roles expose a literal-or-derived switch with base-color selection plus optional `rotate_hue`, `mix`, and `alpha` controls as defined in [docs/theme_configuration.md](theme_configuration.md).
- Selecting the `fonts` section exposes a global font-family selector. It is blank when configured font roles use mixed families, shows the family when all font roles match, applies a selected family to all font roles, and reverts the full font set through `Revert Font Changes`.
- Metric leaves whose ids begin with `board.temp.` or `board.fan.` also expose a live `Binding` selector for the matching board-sensor mapping. Fallback-backed metric leaves, including `[metrics] gpu.fan` and `[metrics] gpu.temp`, expose the same selector only while the current telemetry snapshot is actually using that board binding as the metric value source.
- Empty board-metric bindings are auto-detected from the provider sensor-name list: `cpu` and `gpu` logical bindings use the first sensor name containing the same term, and `system` logical bindings use the first fan sensor name containing `system` or `sys`. The `gpu` fan binding is requested by `gpu.fan` as a fallback source when the selected GPU provider does not expose fan RPM, and the `cpu` temperature binding is requested by `gpu.temp` as the Intel temperature fallback source. Auto-detected bindings become live configuration and are saved by the next `Save Config`.
- The board-metric `Binding` selector keeps the last discovered provider sensor-name list available for config editing even if a later live board sample omits that metadata.
- Multiple logical board metrics can bind to the same provider sensor name, and each bound row shows that same live value.

## Reorder And Drag Behavior

- Metric-list widgets support row reorder handles and add-row affordances when a complete new row fits inside the widget bounds. Dragging a row reorder handle keeps that row and its active handle under the pointer with the active dashed outline, draws the widget underneath with the dragged row's current slot empty, reorders as soon as the pointer reaches another row slot, and snaps the row into its slot on release.
- Row and column layout containers support child reorder handles for every non-empty child slot. Row containers show up/down handles near each child slot's right side and drag vertically; column containers show left/right handles near each child slot's top side and drag horizontally.
- Top-level dashboard row and column reorder handles reveal with the exposed dashboard chrome, alongside dashboard gap handles and layout guides.
- Container reorder handles stay near their preferred side while shifting along or inward from that side to avoid widget-level edit anchors; the rendered handle position is the source of truth for the hitbox.
- During a child reorder drag, the dragged child tracks the pointer with the active dashed outline, the active handle tracks the pointer with its normal single-pixel glyph stroke, the container underneath keeps the dragged child's current slot empty, the child order updates as soon as the pointer reaches another child slot, and the child snaps into its slot on release.
- Layout child reorder handles expose tooltips with the edited layout expression on the first line and localized left/right or up/down reorder guidance on the second line.
- Layout-edit title anchors, size guides, gap handles, selection outlines, and hover outlines that belong to a dragged row or dragged container child are tagged to that target, follow its translated drag position until the drag ends, and draw above the dragged content. Highlights tagged to fixed dashboard content stay behind the dragged target.

## Edit Targets

- Gauge, throughput, metric-list, drive-usage, text, card-chrome, dashboard-spacing, and container-split targets all stay editable through the shared layout-edit interaction model rather than through one-off editors.
- Layout-edit active regions expose at least one 4x4 logical hit patch inside each registered mouse-reactive region, and independent edit handles do not overlap in a way that makes either handle unreachable.

## Layout Guide Sheet

- `CaseDashHeadless.exe` can export a layout guide sheet built from live layout-edit active-region geometry and tooltip text.
- The layout guide sheet feature contract, callout selection, visual styling, placement behavior, and diagnostics export behavior are defined in [docs/layout_guide_sheet.md](layout_guide_sheet.md).
