# System Telemetry layout language specification

## Overview

`config.ini` uses a compact INI-based layout language.
This document is the single maintained source of truth for the language syntax, section ownership, and inline examples, while [resources/config.ini](../resources/config.ini) is the single maintained source of truth for the shipped configuration shape and entry spellings.

### Static behavior

The language is static:

- layout is loaded from config
- the active named layout is selected by `display.layout`
- coordinates are computed during config load or reload
- rendering uses those precomputed coordinates until the next reload

### Dashboard focus

The language is centered around the dashboard shape:

- the dashboard-level `cards` layout places cards through generic containers
- cards contain a small static composition of known widget kinds
- card layouts may also reference another card id as a reusable sub-layout node

## Compact rules

- weights are written as `name:weight(...)` or `name:weight`
- layout containers are written as `kind(...)`
- size pairs are written as `x,y`
- font specs are written as `face,size,weight`
- widget parameters are written inline as `widget(key=value)` or `widget(metric.ref)`
- omitted weights default to `1`
- whitespace around commas is ignored

## Structure

The language has nine levels:

1. Widget-specific sizing sections such as `[metric_list]`, `[drive_usage_list]`, `[throughput]`, `[gauge]`, `[text]`, and `[network_footer]`
2. Runtime selection sections such as `[display]`, `[network]`, and `[storage]`
3. Board sensor mapping in `[board]`
4. Named dashboard structure sections in `[layout.<name>]`
5. Shared dashboard outer spacing in `[dashboard]`
6. Shared card chrome and inner spacing in `[card_style]`
7. Shared dashboard palette in `[colors]`
8. Shared dashboard fonts in `[fonts]`
9. Card-local title, icon, and content composition in `[card.<id>]`

## Runtime selection sections

`[display]` owns executable-relative display selection and placement settings:

- `monitor_name = ...`
- `layout = ...`
- `wallpaper = ...`
- `position = x,y`

`[network]` owns runtime adapter selection:

- `adapter_name = ...`

`[storage]` owns runtime storage-drive selection:

- `drives =`

`[board]` owns the mapping from logical layout metric names to board-specific sensor titles:

- `board.temp.<name> = sensor title`
- `board.fan.<name> = sensor title`

## Widget sections

Widget-specific sizing lives in dedicated sections named exactly after the widget.

Supported widget geometry keys:

- `[metric_list]`: `label_width`, `bar_height`, `row_gap`
- `[drive_usage_list]`: `label_gap`, `activity_width`, `rw_gap`, `bar_gap`, `percent_gap`, `free_width`, `bar_height`, `header_gap`, `row_gap`, `activity_segments`, `activity_segment_gap`
- `[throughput]`: `header_gap`, `axis_padding`, `guide_stroke_width`, `plot_stroke_width`, `leader_diameter`
- `[gauge]`: `outer_padding`, `ring_thickness`, `sweep_degrees`, `segment_count`, `segment_gap_degrees`, `value_bottom`, `label_bottom`
- `[text]`: `bottom_gap`
- `[network_footer]`: `bottom_gap`

`[layout_editor]` owns interactive layout-edit affordance tuning:

- `size_similarity_threshold`

## Dashboard sections

Each `[layout.<name>]` section owns only one named dashboard size and card-placement layout.

Supported `[layout.<name>]` keys:

- `description = popup label suffix`
- `window = width,height`
- `cards = rows(columns:weight(card:weight,...), ...)`

`[dashboard]` owns the outer dashboard spacing:

- `outer_margin = pixels`
- `row_gap = pixels`
- `column_gap = pixels`

`[card_style]` owns shared card chrome and internal spacing:

- `card_padding = pixels`
- `card_radius = pixels`
- `card_border = pixels`
- `header_icon_size = pixels`
- `header_icon_gap = pixels`
- `header_content_gap = pixels`
- `row_gap = pixels`
- `column_gap = pixels`

Card headers size themselves from the larger of the configured header icon size and the measured title text height. `header_content_gap` applies only when the card renders a title or icon header.

Config sections, config keys, card ids, container names, widget names, icon names, and metric references are case-sensitive and must use their documented spelling exactly.
Undocumented key spellings and metric aliases are invalid.

Example:

- `[layout.5x3]`
- `description = 5" 800x480 screen`
- `cards = rows(columns:3(cpu,gpu), columns:2(network:4,storage:9,time:3))`

`[colors]` owns the shared dashboard palette and uses `#RRGGBB`:

- `background_color`
- `foreground_color`
- `accent_color`
- `layout_guide_color`
- `active_edit_color`
- `panel_border_color`
- `muted_text_color`
- `track_color`
- `panel_fill_color`
- `graph_background_color`
- `graph_axis_color`
- `graph_marker_color`

`[fonts]` owns the shared dashboard fonts:

- `title = face,size,weight`
- `big = face,size,weight`
- `value = face,size,weight`
- `label = face,size,weight`
- `text = face,size,weight`
- `small = face,size,weight`
- `footer = face,size,weight`
- `clock_time = face,size,weight`
- `clock_date = face,size,weight`

## Card sections

Each card section uses:

- `title = ...` when the card shows a header title
- `icon = ...` when the card shows a header icon
- `layout = ...`

When both `title` and `icon` are omitted, the card has no header and the card content starts after the card padding.

Card layouts may reference another card id as a leaf layout node when they want to reuse that card's inner layout:

- `layout = columns(storage_throughput:5, storage_usage:7)`

Recommended section names:

- `[card.cpu]`
- `[card.gpu]`
- `[card.network]`
- `[card.storage]`
- `[card.time]`

Future cards follow the same pattern:

- `[card.memory]`
- `[card.board]`
- `[card.weather]`

## Icon names

`icon` values are resource names, not file paths.

Supported icon names:

- `cpu`
- `gpu`
- `network`
- `storage`
- `time`

Mixed-case icon spellings such as `CPU` or `Network` are invalid.

## Layout expressions

Supported layout kinds:

- `rows(...)`
- `columns(...)`

### Meaning

- `rows(...)` splits content vertically into weighted children, with child weights written as `child:weight`
- `columns(...)` splits content horizontally into weighted children, with child weights written as `child:weight`

`metric_list(...)` rows use a computed row height of `value_font_height + [metric_list].row_gap + [metric_list].bar_height`, then center the stacked value text and bar inside that row band with no internal gap.
`drive_usage_list(...)` uses a header height of `small_font_height + [drive_usage_list].header_gap`, then rows use a computed row height of `max(label_font_height,small_font_height,[drive_usage_list].bar_height) + [drive_usage_list].row_gap`.
`text(...)` uses a preferred height of `text_font_height + [text].bottom_gap`.
`network_footer` uses a preferred height of `footer_font_height + [network_footer].bottom_gap`.
`vertical_spacer(widget_name)` uses the preferred height of the referenced widget type.
`clock_time` uses a preferred height of `clock_time_font_height`.
`clock_date` uses a preferred height of `clock_date_font_height`.
`throughput(...)` uses a preferred height of `small_font_height + [throughput].header_gap + small_font_height`.
Throughput header labels use their actual rendered text width at draw time.
Drive label width and drive percent width are measured from the configured fonts at layout load.
Throughput axis width is measured from the configured fonts at layout load, then widened by `[throughput].axis_padding`.

Nested layout expressions are allowed.
Inside a `[card.<id>]` layout, a leaf identifier that matches another card id is a card-layout reference and resolves to that referenced card's layout during layout resolution.
In a vertical `rows(...)` container, fixed-height direct children such as `text`, `network_footer`, and `vertical_spacer(...)` keep their preferred height and the remaining space goes to flexible siblings when no `vertical_spring` is present.
When one or more direct `vertical_spring` children are present in `rows(...)`, every spring absorbs the remaining height before weighted stretching and multiple springs divide that height by weight.
Interactive layout editing always exposes the container guides for `rows(...)` and `columns(...)`, reseeds dragged container weights from the current resolved child extents, snaps to the nearest same-type exact-size group as soon as the similarity ruler threshold is reached by iteratively re-evaluating nested weighted layouts, lets `Alt` temporarily bypass that snap and continue free dragging, then saves the updated integer weights back into the same `name:weight(...)` expression structure.
When the pointer is outside every card in layout-edit mode, the dashboard layout shows one horizontal top-left anchor for `[dashboard].outer_margin` plus at most one `row_gap` gap anchor and one `column_gap` gap anchor on the first encountered rendered `rows(...)` and `columns(...)` containers, using `[dashboard].row_gap` or `[dashboard].column_gap`.
When the pointer hovers the top band of a card in layout-edit mode, up to the top edge of the content area, the renderer shows that card's shared `[card_style]` guides and anchors.
When the pointer is anywhere inside a rendered card in layout-edit mode, that card shows at most one `row_gap` gap anchor and one `column_gap` gap anchor on the first encountered rendered `rows(...)` and `columns(...)` containers inside the card, using `[card_style].row_gap` or `[card_style].column_gap`.
`[card_style].card_padding` exposes a horizontal guide at the top inner edge of the card padding band.
`[card_style].card_radius` exposes a square anchor at the top-left rounded-corner tangent point.
`[card_style].card_border` exposes a circular radial size anchor centered on the top card border.
Hovering a card header icon exposes a dotted icon highlight plus a square upper-right anchor for `[card_style].header_icon_size`.
`[card_style].header_icon_gap` exposes a vertical guide at the start of the card title text when the card renders both an icon and title.
`[card_style].header_content_gap` exposes a horizontal guide at the top edge of the card content area when the card renders a header.
`row_gap` gap anchors render as vertical lines on the left edge of the topmost rendered gap with T-caps at both ends and a square drag handle at the bottom end; dragging that handle down increases the gap.
`column_gap` gap anchors render as horizontal lines on the top edge of the leftmost rendered gap with T-caps at both ends and a square drag handle at the right end; dragging that handle right increases the gap.
`outer_margin` renders as a horizontal gap anchor in the top-left window corner, spanning from the left window edge to the dashboard content left edge at the dashboard content top edge, and dragging its right-end handle right increases the margin.
When the pointer hovers a non-empty widget in layout-edit mode, the renderer outlines that widget's resolved box and shows any widget-local size guides that widget supports.
`metric_list(...)` exposes a vertical guide for `[metric_list].label_width` at the label/value split.
`metric_list(...)` also exposes horizontal guides after each visible non-empty row for `[metric_list].row_gap`.
`throughput(...)` exposes a vertical guide for `[throughput].axis_padding` at the graph's left plot edge so the scale gutter can be widened or narrowed live.
`throughput(...)` also exposes a horizontal guide for `[throughput].header_gap` at the boundary between the value header row and the graph body.
`throughput(...)` also exposes circular radial size anchors for `[throughput].leader_diameter` at the middle of the plot's right edge, `[throughput].plot_stroke_width` at the middle of the plot's left edge, and `[throughput].guide_stroke_width` on a centered horizontal guide line whose center stays fixed while the stroke width changes; each anchor circle renders slightly larger than the edited size and dragging it in any direction resizes that throughput chrome from the pointer distance to the anchor center.
`gauge(...)` also exposes concentric circular radial size anchors centered on the gauge, with the outer circle editing `[gauge].outer_padding` and the inner circle editing `[gauge].ring_thickness`.
Dragging the `gauge` `segment_gap_degrees` guide and the `drive_usage_list` `activity_segment_gap` guide clamps those edits against the current live geometry, while later layout resolution still keeps every segment drawable if another config change or a loaded file leaves a larger saved gap behind.
`drive_usage_list(...)` exposes vertical guides for `[drive_usage_list].label_gap` at the left edge of the `R` activity column, `[drive_usage_list].rw_gap` at the left edge of the `W` activity column, `[drive_usage_list].bar_gap` at the left edge of the usage bar, `[drive_usage_list].percent_gap` at the right edge of the usage bar, `[drive_usage_list].activity_width` at the right edge of the `W` activity column, and `[drive_usage_list].free_width` on the free-space column, plus horizontal guides for `[drive_usage_list].activity_segment_gap` at the top edge of the lowermost read/write segment, `[drive_usage_list].header_gap` below the header band, and `[drive_usage_list].row_gap` after each visible non-empty row.
`drive_usage_list(...)` also exposes a diamond anchor centered on the top edge of the read/write activity band so `[drive_usage_list].activity_segments` can be dragged live in either axis.

Example:

- `columns(rows:5(throughput:4(storage.read),throughput:4(storage.write)), rows:7(drive_usage_list,vertical_spring))`
- `columns(storage_throughput:5, storage_usage:7)`

## Widget names

Supported widget names:

- `text`
- `gauge`
- `metric_list`
- `throughput`
- `network_footer`
- `vertical_spacer`
- `vertical_spring`
- `drive_usage_list`
- `clock_time`
- `clock_date`

Legacy widget aliases are not supported. Bind telemetry through the documented generic widget names and explicit parameters, for example `text(cpu.name)`, `gauge(cpu.load)`, or `throughput(network.upload)`.
Mixed-case widget or container spellings such as `Throughput(...)` or `Columns(...)` are invalid.

## Widget parameters

- Widgets may bind data inline.
- For widgets that accept a list, the plain comma-separated body is passed through as that widget's parameter string.
- `metric_list(...)` items may append `=Label` to override the rendered row label for that metric.

Examples:

- `text(cpu.name)`
- `gauge(gpu.load)`
- `metric_list(cpu.ram,board.temp.cpu=Temp,cpu.clock,board.fan.cpu,board.fan.system=System Fan)`
- `throughput(network.upload)`
- `drive_usage_list`
- `vertical_spacer(network_footer)`

`vertical_spacer(widget_name)` reserves the referenced widget's vertical layout space without drawing content.
`vertical_spring` draws nothing and absorbs remaining height inside a vertical `rows(...)` container.

Supported metric references include:

- `cpu.name`
- `cpu.load`
- `cpu.clock`
- `cpu.ram`
- `board.temp.cpu`
- `board.fan.cpu`
- `board.fan.system`
- `gpu.name`
- `gpu.load`
- `gpu.temp`
- `gpu.clock`
- `gpu.fan`
- `gpu.vram`
- `network.upload`
- `network.download`
- `storage.read`
- `storage.write`

## Drive and sensor selection

Runtime storage-drive selection lives in `[storage]`, while board-sensor selection comes from layout metric references.

Example:

- `[storage]`
- `drives =`
- `metric_list(cpu.ram=RAM,board.temp.cpu=Temp,board.fan.cpu=Fan,board.fan.system=System Fan)`

- `[storage] drives` defines the vertical drive-usage list contents and order.
- An empty `[storage] drives` value means the storage telemetry selection flow auto-selects all currently available fixed drives.
- Layout metric references define which logical board temperature and fan metrics are requested from the board provider.
- The `[board]` section maps those logical metric names to the board-specific sensor titles that the provider looks up.

## Consistency rules

Shared absolute geometry belongs only in `[layout]`, including:

- window size
- cards layout tree

Shared dashboard spacing belongs only in `[dashboard]`, including:

- margins
- row and column gaps

Shared card chrome and inner spacing belong only in `[card_style]`, including:

- border radius and border width
- card padding
- measured header size, icon size, and icon gap
- header-to-content spacing when a card renders a header
- column gap
- `row_gap`

Shared visual styling belongs only in:

- colors in `[colors]`
- fonts in `[fonts]`

Card sections define relative structure only:

- card title
- card icon
- layout expression
- widget parameters
- relative child weights using the `name:weight(...)` form

## Static sizing rules

Layout is resolved without using live telemetry values.

Static sizing rules:

- header height comes from config
- metric rows and drive rows derive their packed heights from measured font metrics plus their configured bar heights and vertical gaps
- drive-usage widgets reserve a dedicated header row and use fixed-width read/write activity columns from `[drive_usage_list]`
- widget-specific heights, paddings, widths, segment counts, and gauge/throughput chrome come from their matching widget sections, while fixed text columns, text-widget preferred heights, footer preferred heights, `vertical_spacer(...)` mirrored preferred heights, and clock preferred heights derive directly from their documented measured font metrics plus their matching documented padding where applicable
- throughput sections follow the configured vertical rhythm
- centered time/date layouts use `rows(vertical_spring, ..., vertical_spring)` so springs balance the surrounding free space
- repeated lists such as drives divide their assigned area using the item count from config

## Validation rules

- every dashboard container weight must be positive
- every card referenced from `cards` must have a matching `[card.<id>]` section
- every icon name must resolve to a supported embedded resource
- every widget name must resolve to a supported hard-coded widget kind

## Reference

See [resources/config.ini](../resources/config.ini) for the maintained example of the
language in use. That file is the single source of truth for the checked-in layout shape and supported
spelling of the shipped configuration.
