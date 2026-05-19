# CaseDash Layout Language

This document owns the config language, section ownership, syntax, and validation rules.
See also: [resources/config.ini](../resources/config.ini) for the maintained shipped example and entry spellings, [docs/glossary.md](glossary.md) for project terminology, [docs/theme_configuration.md](theme_configuration.md) for theme and color value syntax, [docs/specifications.md](specifications.md) for runtime behavior, and [docs/diagnostics.md](diagnostics.md) for diagnostics-only switches and workflows.

## Purpose And Scope

`config.ini` uses a compact INI-based language that selects runtime targets, defines metric presentation, and describes the static dashboard layout. The runtime loads one active named layout at a time and resolves card and widget coordinates from config during load or reload rather than from live telemetry values.

## Compact Rules

- Section names, keys, layout identifiers, card ids, widget names, icon names, and metric ids are case-sensitive.
- Size pairs use `x,y`.
- Font specs use `face,size,weight`.
- Composite metric definitions use `<scale>,<unit>,<label>`.
- Container weights use `name:weight` or `name:weight(...)`.
- Omitted weights default to `1`.
- Widget parameters are written inline as `widget(...)`.
- Date and time widget format strings are inline widget parameters.
- Whitespace around commas is ignored.

## Section Ownership

The language is divided by responsibility:

- Widget sections own widget-local geometry: `[metric_list]`, `[drive_usage_list]`, `[throughput]`, `[gauge]`, `[text]`, `[network_footer]`, and `[layout_editor]`.
- Runtime selection sections own runtime target choice: `[display]`, `[gpu]`, `[network]`, and `[storage]`.
- `[board]` owns logical board-metric to provider-sensor bindings.
- `[metrics]` owns metric presentation and normalization definitions.
- `[theme.<name>]` owns base color tokens and the user-visible theme description.
- `[layout.<name>]` owns named dashboard geometry and top-level card placement.
- `[dashboard]` owns outer dashboard spacing.
- `[card_style]` owns shared card chrome and in-card spacing.
- `[colors]` owns the shared dashboard palette.
- Color values use the literal and derived color syntax defined in [docs/theme_configuration.md](theme_configuration.md).
- `[layout_guide_sheet]` owns diagnostics-only layout guide sheet callout colors and layout constants; these fields are loaded from config but are not editable through the layout-edit dialog.
- `[fonts]` owns the shared font roles.
- `[card.<id>]` owns card-local title, icon, and inner layout composition.

`resources/config.ini` remains the maintained source for the shipped section set and concrete key spellings.

## Runtime Sections

`[display]` selects the target monitor, active named layout, wallpaper, placement, and optional explicit render scale.

`[gpu]` selects the preferred GPU adapter by display name. An empty or missing `adapter_name` means the runtime selects the first non-software DXGI adapter.

`[network]` selects the preferred runtime adapter.

`[storage]` selects the drive letters to show in the drive-usage list. An empty drive selection means the runtime resolves all currently available fixed drives.

`[board]` maps logical metric ids in the `board.temp.*` and `board.fan.*` families to provider-specific sensor names. Empty `cpu`, `gpu`, and `system` mappings are resolved from matching provider sensor names at runtime and are persisted by the next `Save Config`. The `gpu` fan binding is used as the fallback source for `gpu.fan` when the selected GPU provider does not expose fan RPM, and the `cpu` temperature binding is used as the Intel fallback source for `gpu.temp` when the selected Intel GPU provider does not expose native temperature.

`[metrics]` defines the display registry used by bound widgets:

- Format: `metric.id = <scale>,<unit>,<label>`
- `*` scale means the widget normalizes against telemetry-provided scale data.
- Display style is metadata-owned for the metric id and is not a config field.
- The runtime-only `nothing` placeholder keeps built-in metadata and `[metrics]` entries for it are ignored.
- Throughput widgets and drive-usage rows read their displayed labels and units from this registry rather than from widget-local text.

## Containers, Widgets, And Parameters

Supported container kinds:

- `rows(...)`
- `columns(...)`

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

Supported icon names:

- `cpu`
- `gpu`
- `network`
- `storage`
- `time`

Card sections use:

- `title = ...`
- `icon = ...`
- `layout = ...`

Card layouts may reference another card id as a leaf node to reuse that card's inner composition.

Widget parameter rules:

- `text(...)`, `gauge(...)`, and `throughput(...)` bind one metric id.
- `metric_list(...)` binds a comma-separated list of metric ids.
- `clock_time(...)` and `clock_date(...)` require a format string parameter.
- The shipped time format is `HH:MM`; the shipped date format is `YYYY-MM-DD`.
- Time format tokens are `HH`, `H`, `hh`, `h`, `MM`, `M`, `SS`, `S`, `AM`, and `am`.
- Date format tokens are `YYYY`, `YY`, `MMMM`, `MMM`, `MM`, `M`, `DD`, `D`, `dddd`, and `ddd`.
- `drive_usage_list` and `vertical_spring` take no inline parameter payload.
- `vertical_spacer(widget_name)` reserves the preferred height of the referenced widget type without drawing content.

Supported configurable metric ids include built-in CPU, GPU, network, storage, and drive metrics plus configured `board.temp.*` and `board.fan.*` logical metrics. The runtime-only `nothing` placeholder remains valid in metric-list bindings but does not belong in `[metrics]`.

## Static Sizing Rules

The language defines static layout behavior rather than live interaction behavior:

- Named layouts resolve one dashboard window size plus a top-level `cards` composition.
- Dashboard spacing belongs only in `[dashboard]`.
- Shared card chrome and in-card spacing belong only in `[card_style]`.
- Shared visual styling belongs only in `[colors]`, `[theme.<name>]`, and `[fonts]`; diagnostics-only layout guide sheet chrome and spacing belongs in `[layout_guide_sheet]`.
- Card sections define relative composition only: title, icon, layout tree, widget bindings, and child weights.
- Metric-list row height derives from measured fonts plus `[metric_list]` bar and gap settings.
- Drive-usage header and row heights derive from measured fonts plus `[drive_usage_list]` geometry settings.
- `text(...)` and `network_footer` preferred heights derive from their font roles plus their bottom-gap settings.
- `clock_time` and `clock_date` preferred heights derive directly from their dedicated font roles.
- `throughput(...)` preferred height derives from the measured header row, configured header gap, and measured plot-label band.
- Gauge preferred height derives from gauge geometry plus measured gauge text metrics.
- `vertical_spacer(widget_name)` mirrors the preferred height of the referenced widget type.
- In `rows(...)`, fixed-height direct children keep their preferred height and the remaining height goes to flexible siblings.
- `vertical_spring` absorbs the remaining height in `rows(...)` and divides it by weight across multiple springs.
- Layout resolution uses live-independent geometry; telemetry values do not change preferred size decisions.

## Validation Rules

- Every dashboard and container weight must be positive.
- Every referenced card id in the active layout must have a matching `[card.<id>]` section.
- Every icon name must resolve to a supported embedded resource name.
- Every widget or container name must use a supported canonical token.
- Only documented spellings are valid for keys, section families, widget names, icon names, and metric ids.
- Overlaying executable-side config replaces named-layout and card-layout expressions instead of duplicating them.

## Minimal Example

```ini
[display]
layout = 5x3

[layout.5x3]
window = 800,480
cards = rows(columns:3(cpu,gpu), columns:2(network:4,storage:9,time:3))

[card.cpu]
title = CPU
icon = cpu
layout = columns(gauge(cpu.load), metric_list(cpu.ram,board.temp.cpu,cpu.clock,board.fan.cpu))
```

See [resources/config.ini](../resources/config.ini) for the maintained full example, the shipped default layouts, and the authoritative spelling of supported entries.
