# Display Package

`src/display/` owns monitor enumeration, placement, configure-display support, wallpaper helpers, display constants, and scale-related display behavior.

## Responsibilities

- Enumerate monitors and expose display identities used by runtime placement and configure-display flows.
- Generate the flat `Configure Display` option list with one section per monitor, fullscreen entries for matching aspect ratios, and top/bottom or left/right edge-placement entries for nonmatching aspect ratios.
- Provide placement schematic geometry for the dashboard shell menu icons that show display aspect ratio and the shaded CaseDash footprint.
- Resolve saved placement against current monitor availability.
- Keep watching for a configured `display.monitor_name` when login startup or monitor hotplug races ahead of enumeration.
- Let `WM_DPICHANGED` apply cross-monitor DPI transitions before destination window size scaling.
- Build display config updates from menu choices, including explicit scale, logical position, wallpaper ownership, committed active-row state, and committed wallpaper owner transitions.
- Generate configure-display active-row state from the dashboard controller's committed display snapshot while still using the live layout config for option sizing and schematic geometry.
- Render the configure-display active state into the shared placement schematic bitmap as the native selected background shared with the hover-titlebar edit-layout button; these placement rows do not use native menu checkmarks because the bitmap occupies the checkmark/icon slot.
- Round resolved display scales to three decimal places before using them for window sizing or saved display placement.
- Provide the pure aspect-preserving interactive resize scale helper used by dashboard resize mode before committing explicit display scale and placement.
- Resolve committed CaseDash wallpaper ownership from config plus monitor geometry so stale nonfullscreen `display.wallpaper` values are normalized during committed saves and exports.
- Support wallpaper and configure-display helpers used by dashboard shell commands. Fullscreen choices render and apply the CaseDash blank wallpaper; edge placements save config without creating new wallpaper and clear previous committed CaseDash wallpaper ownership.

## Boundaries

- Display owns monitor and placement primitives; dashboard shell owns user command presentation.
- Runtime behavior for display selection, move mode, and lower-right resize mode is specified in [docs/specifications.md](../specifications.md).
