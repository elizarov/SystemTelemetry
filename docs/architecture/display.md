# Display Package

`src/display/` owns monitor enumeration, placement, configure-display support, wallpaper helpers, display constants, and scale-related display behavior.

## Responsibilities

- Enumerate monitors and expose display identities used by runtime placement and configure-display flows.
- Resolve saved placement against current monitor availability.
- Keep watching for a configured `display.monitor_name` when login startup or monitor hotplug races ahead of enumeration.
- Let `WM_DPICHANGED` apply cross-monitor DPI transitions before destination window size scaling.
- Support wallpaper and configure-display helpers used by dashboard shell commands.

## Boundaries

- Display owns monitor and placement primitives; dashboard shell owns user command presentation.
- Runtime behavior for display selection and move mode is specified in [docs/specifications.md](../specifications.md).
