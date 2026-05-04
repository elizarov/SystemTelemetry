# Config Package

`src/config/` owns the persisted configuration model, parser, writer, resolver, schema metadata, theme color resolver, color expression parser, shared OKLab/OKLCH and HSV color math, and config-facing contract types.

## Responsibilities

- Parse the embedded config template and executable-side overlay into `AppConfig` and `LayoutConfig`.
- Preserve text-oriented save behavior separately from higher-level config resolution.
- Write minimal overlay saves and full embedded-template-shaped exports.
- Own offset-based runtime field descriptors and config schema reflection data.
- Own config-facing enums and DTOs such as widget class, metric display style, telemetry settings, layout fields, and color expressions.
- Keep shared color-space conversion and expression resolution inside config so dialog and renderer callers do not carry duplicate color math.
- Validate metric ids through the injected `ConfigMetricCatalog` contract. The production metric catalog remains owned by `telemetry`.

## Boundaries

- `config` may depend only on `config` and `util`.
- Config parsing does not reach upward into telemetry, widgets, dashboard UI, diagnostics, or renderer implementation details.
- Runtime-only placeholder metric metadata stays outside persisted config so `[metrics]` remains limited to configurable metric definitions.
- C++-side synthesized fallback layout, card, widget, font, color, or styling defaults do not duplicate the embedded template.
