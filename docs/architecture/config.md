# Config Package

`src/config/` owns the persisted configuration model, parser, writer, resolver, schema metadata, theme color resolver, color expression parser, shared OKLab/OKLCH and HSV color math, and config-facing contract types.

## Responsibilities

- Parse the embedded config template and executable-side overlay into `AppConfig` and `LayoutConfig`.
- Preserve text-oriented save behavior separately from higher-level config resolution.
- Write minimal overlay saves and full embedded-template-shaped exports.
- Own the hand-maintained config schema in `src/config/config.h`, primitive custom-section annotations in `src/config/config_primitives.h`, generated runtime field metadata under `build\cmake\generated\config\`, and generated layout-edit metadata under `build\cmake\generated\layout_model\`.
- Own config-facing enums and DTOs such as widget class, metric display style, telemetry settings, layout fields, and color expressions.
- Own the shared metric-to-board-binding resolver for direct `board.temp.*` and `board.fan.*` metrics plus provider fallback metrics such as `gpu.temp` and `gpu.fan`.
- Own config-language color text formatting, including the canonical `#RRGGBBAA` spelling shared by config saves, diagnostics traces, layout-edit text, and dialog traces.
- Keep shared color-space conversion and expression resolution inside config so dialog and renderer callers do not carry duplicate color math.
- Validate metric ids through the injected `ConfigMetricCatalog` contract. The production metric catalog remains owned by `telemetry`.

## Boundaries

- `config` may depend only on `config` and `util`.
- Config parsing does not reach upward into telemetry, widgets, dashboard UI, diagnostics, or renderer implementation details.
- Telemetry fallback, layout binding collection, and layout edit dialog controls consume the config-owned board-binding resolver and runtime active-binding helper instead of carrying separate fallback target tables.
- Runtime-only placeholder metric metadata stays outside persisted config so `[metrics]` remains limited to configurable metric definitions.
- Config metadata generation derives field keys from C++ member names, or from explicit `rename=` descriptor attributes when a C++ spelling must differ, and validates persisted spellings against `resources/config.ini`.
- C++-side synthesized fallback layout, card, widget, font, color, or styling defaults do not duplicate the embedded template.
