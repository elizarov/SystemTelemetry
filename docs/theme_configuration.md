# Theme Configuration Sublanguage

This specification defines the color theme sublanguage used to select a named theme and derive application colors from that theme.

## File structure

A configuration contains the active theme selector in `[display]`, one or more theme sections, and color role sections.

```toml
[display]
theme = dark_cyan

[theme.dark_cyan]
background = #000000FF
foreground = #FFFFFFFF
accent = #00BFFFFF
guide = #FF6A00FF

[colors]
background_color = background
foreground_color = foreground
```

## Theme selection

`theme` names the active theme.

```toml
theme = dark_cyan
```

The value must match an existing `[theme.<name>]` section.

## Theme sections

Each theme is defined in a section named:

```toml
[theme.<name>]
```

A standard theme defines these base tokens:

```toml
[theme.dark_cyan]
background = #000000FF
foreground = #FFFFFFFF
accent = #00BFFFFF
guide = #FF6A00FF
```

Token meanings:

| Token | Meaning |
|---|---|
| `background` | Main app background. |
| `foreground` | Primary text and icon color. |
| `accent` | Main highlight color for graphs, meters, bars, and active data. |
| `guide` | Base guide/edit overlay color. |

## Color role sections

Color role sections map application-specific color names to literal colors or to colors derived from the active theme.

Sections are:

```toml
[colors]
```

and:

```toml
[layout_guide_sheet]
```

## Color values

A color value must be one of:

```toml
#RRGGBBAA
identifier
identifier(option: value, option: value)
```

Examples:

```toml
accent_color = accent
peak_ghost_color = accent(alpha: 0x60)
panel_fill_color = background(mix: foreground 0.035)
```

## Literal colors

Literal colors use 8-digit hexadecimal RGBA:

```toml
#RRGGBBAA
```

The canonical form is uppercase hex.

## Identifiers

Inside color role sections, a bare identifier resolves through the reference rules below.

Example:

```toml
[display]
theme = dark_cyan

[theme.dark_cyan]
accent = #00BFFFFF

[colors]
accent_color = accent
```

`accent` resolves to `theme.dark_cyan.accent`.

Color roles may reference other color roles:

```toml
[colors]
layout_guide_color = guide
active_edit_color = guide(rotate_hue: 35)

[layout_guide_sheet]
callout_border_color = guide(mix: active_edit_color 0.35)
```

## Derived Color Expression Syntax

A derived color expression always starts with a base color identifier:

```toml
base_color(option: value, option: value)
```

The supported options are:

```text
rotate_hue: <number>
mix: <color> <amount>
alpha: <alpha>
```

## `rotate_hue`

`rotate_hue` rotates the hue of the base color by a relative number of degrees.

```toml
active_edit_color = guide(rotate_hue: 35)
```

Syntax:

```text
rotate_hue: <number>
```

Rules:

- The value is always degrees.
- Positive and negative values are allowed.
- The color's alpha is preserved.
- Hue rotation is evaluated in OKLCH.

The language intentionally does not support absolute hue assignment. Theme-relative hue rotation keeps derived guide/edit colors compatible with different themes.

## `mix`

`mix` blends the base color toward another color.

```toml
panel_fill_color = background(mix: foreground 0.035)
```

Syntax:

```text
mix: <color> <amount>
```

Rules:

- `<color>` is an identifier that resolves to a color.
- `<amount>` must be from `0.0` to `1.0`.
- `0.0` returns the base color.
- `1.0` returns the target color.
- Mixing is evaluated in OKLab.

Alpha is interpolated together with RGB unless an explicit `alpha` option is also present.

## `alpha`

`alpha` replaces the alpha channel of the derived color.

```toml
peak_ghost_color = accent(alpha: 0x60)
```

## Operation Order

When multiple options are present, they are applied in this fixed order:

1. Start with the base color.
2. Apply `rotate_hue`, if present.
3. Apply `mix`, if present.
4. Apply `alpha`, if present.

Example:

```toml
callout_leader_color = guide(rotate_hue: 28, alpha: 0xE6)
```

means:

1. Start with `guide`.
2. Rotate hue by `28` degrees.
3. Replace alpha with `0xE6`.

## Dependency Rules

Color references form a limited dependency graph.

Rules:

- `[colors]` references may point only to entries in the active theme section.
- `[layout_guide_sheet]` references may point to `[colors]` entries or entries in the active theme section.

## Overrides

Any color role may be replaced with a literal color.

```toml
[colors]
panel_border_color = #1E2837FF
```

Literal colors are terminal overrides and do not depend on theme tokens.

## Runtime Theme Selection

The dashboard popup menu and tray menu include a `Theme` submenu immediately after `Layout`.

The `Theme` submenu:

- Lists all configured `[theme.<name>]` sections.
- Marks the active `[display] theme` value.
- Applies a selected theme immediately and repaints the dashboard.
- Keeps layout-edit mode active when a theme is selected.
- Uses the same unsaved edit-session behavior as other live configuration edits.

## Edit Configuration Dialog

The modeless `Edit Configuration` dialog exposes theme editing through the existing config tree and live editor pane.

Theme section behavior:

- Only the active `[theme.<name>]` section selected by `[display] theme` appears in the dialog tree.
- Selecting the active theme section shows one RGBA editor for each standard theme token: `background`, `foreground`, `accent`, and `guide`.
- Theme token edits preview immediately.
- Per-field revert works for theme token edits.

`[colors]` section behavior:

- Each color role has a mode switch with exactly two choices: `Literal` and `Derived`.
- `Literal` mode shows the existing RGBA editor and stores `#RRGGBBAA`.
- `Derived` mode shows controls for the base color and for each supported transformation: `rotate_hue`, `mix`, and `alpha`.
- The base color selector offers the standard active-theme token identifiers: `background`, `foreground`, `accent`, and `guide`.
- Each transformation has an enable switch. Disabled transformations are omitted from the saved color expression.
- Enabled transformations are shown and saved in the fixed operation order: `rotate_hue`, then `mix`, then `alpha`.
- `rotate_hue` exposes a signed degree value.
- `mix` exposes a target color selector with the same active-theme token identifiers and an amount value from `0.0` to `1.0`.
- `alpha` exposes an alpha byte value from `0x00` to `0xFF`.
- Valid edits preview immediately. Invalid or incomplete derived expressions do not preview and do not replace the last valid pending value.
- Per-field revert restores the whole color role expression, including its mode and all transformation controls.
- The dialog displays the effective resolved color preview for both literal and derived roles.

`[layout_guide_sheet]` remains diagnostics-only and is not editable through the dialog.

## Complete example

```toml
[display]
theme = dark_cyan

[theme.dark_cyan]
background = #000000FF
foreground = #FFFFFFFF
accent = #00BFFFFF
guide = #FF6A00FF

[theme.dark_green]
background = #000000FF
foreground = #FFFFFFFF
accent = #00E676FF
guide = #FF6A00FF

[theme.light_blue]
background = #F8FAFCFF
foreground = #111827FF
accent = #007ACCFF
guide = #E85D04FF

[colors]
background_color = background
foreground_color = foreground
icon_color = foreground

accent_color = accent
peak_ghost_color = accent(alpha: 0x60)

layout_guide_color = guide
active_edit_color = guide(rotate_hue: 35)

panel_fill_color = background(mix: foreground 0.035)
graph_background_color = background(mix: foreground 0.055)
panel_border_color = background(mix: accent 0.16)
track_color = background(mix: foreground 0.20)
muted_text_color = foreground(mix: accent 0.22)
graph_axis_color = background(mix: foreground 0.36)
graph_marker_color = background(mix: foreground 0.13)

[layout_guide_sheet]
callout_leader_color = guide(rotate_hue: 28, alpha: 0xE6)
callout_fill_color = background(mix: foreground 0.035, alpha: 0xF5)
callout_border_color = guide(mix: active_edit_color 0.35)
callout_parameter_color = foreground
callout_description_color = muted_text_color
```
