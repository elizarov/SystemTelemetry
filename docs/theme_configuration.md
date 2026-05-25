# Theme Configuration Sublanguage

This specification defines the color theme sublanguage used to select a named theme and derive application colors from that theme.

## File structure

A configuration contains the active theme selector in `[display]`, one or more theme sections, and color role sections.

## Theme selection

`theme` names the active theme.

The value must match an existing `[theme.<name>]` section.

## Theme sections

Each theme is defined in a section named:

```toml
[theme.<name>]
```

A standard theme defines a `description` value and these base tokens:

`description` is the user-visible summary shown next to the theme name in menus and in the theme section editor.

Token meanings:

| Token | Meaning |
|---|---|
| `background` | Main app background. |
| `foreground` | Primary text and icon color. |
| `accent` | Main highlight color for graphs, meters, bars, and active data. |
| `guide` | Base guide/edit overlay color. |

## Color role sections

Color role sections map application-specific color names to literal colors or to colors derived from the active theme.

The dashboard `[colors]` roles include text, icon, accent, peak ghost, warning, panel, track, graph, and layout-edit guide colors. `warning_color` is used for short permission-required indicators such as `!admin`.

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

## Literal colors

Literal colors use 8-digit hexadecimal RGBA:

```toml
#RRGGBBAA
```

The canonical form is uppercase hex.

## Identifiers

Inside color role sections, a bare identifier resolves through the reference rules below.

Active theme token identifiers resolve to the matching token in the selected `[theme.<name>]` section.

`[layout_guide_sheet]` color roles may reference `[colors]` roles in addition to active theme tokens when `CaseDashHeadless.exe` resolves layout guide sheet exports. Those defaults are loaded from the headless-only embedded resource rather than from the shipped app text atlas.

## Derived Color Expression Syntax

A derived color expression always starts with a base color identifier:

```toml
base_color(option: value, option: value)
```

The supported options are:

```text
rotate_hue: <number>
mix: <amount> <color>
alpha: <alpha>
```

## `rotate_hue`

`rotate_hue` rotates the hue of the base color by a relative number of degrees.

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

Syntax:

```text
mix: <amount> <color>
```

Rules:

- `<amount>` must be from `0.0` to `1.0`.
- `<color>` is an identifier that resolves to a color.
- `0.0` returns the base color.
- `1.0` returns the target color.
- Mixing is evaluated in OKLab.

Alpha is interpolated together with RGB unless an explicit `alpha` option is also present.

## `alpha`

`alpha` replaces the alpha channel of the derived color.

## Operation Order

When multiple options are present, they are applied in this fixed order:

1. Start with the base color.
2. Apply `rotate_hue`, if present.
3. Apply `mix`, if present.
4. Apply `alpha`, if present.

## Dependency Rules

Color references form a limited dependency graph.

Rules:

- `[colors]` references may point only to entries in the active theme section.
- `[layout_guide_sheet]` references may point to `[colors]` entries or entries in the active theme section for headless layout guide sheet exports.

## Overrides

Any color role may be replaced with a literal color.

Literal colors are terminal overrides and do not depend on theme tokens.

## Runtime Theme Selection

The dashboard popup menu and tray menu include a `Theme` submenu immediately after `Layout`.

The `Theme` submenu:

- Lists all configured `[theme.<name>]` sections.
- Shows each theme description after its name when one is configured.
- Marks the active `[display] theme` value.
- Applies a selected theme immediately and repaints the dashboard.
- Keeps layout-edit mode active when a theme is selected.
- Uses the same unsaved edit-session behavior as other live configuration edits.

## Edit Configuration Window

The modeless `Edit Configuration` window exposes theme editing through the existing config tree and live editor pane.

Theme section behavior:

- The config tree follows the embedded config template order; the active theme section appears at the position of the shipped `[theme.<name>]` section group.
- Only the active `[theme.<name>]` section selected by `[display] theme` appears in the dialog tree.
- The active theme section description is shown in the right-hand editor pane.
- Selecting the active theme section shows a theme selector dropdown listing all configured themes. Changing the selector updates `[display] theme`, previews immediately, refreshes the tree to the newly active theme section, and keeps the edit session active.
- The active theme section editor shows an equilateral triangle preview below the dropdown. The triangle top edge blends from `background` at the left vertex to `foreground` at the right vertex, the bottom-center vertex is `accent`, and a thin vertical line through the triangle uses `guide`.
- Expanding the active theme section shows one literal color editor for each standard theme token: `background`, `foreground`, `accent`, and `guide`.
- Theme token edits preview immediately.
- Per-field revert works for theme token edits.

`[colors]` section behavior:

- Each color role has a mode switch with exactly two choices: `Literal` and `Derived`.
- `Literal` mode shows the shared literal color editor and stores `#RRGGBBAA`. The editor exposes dialog-local `RGB`, `LCH`, and `HSV` tabs for the same color value, with alpha below the tabs as the shared channel. Each RGB, LCH, or HSV slider has a text-height gradient preview below it that illustrates the slider effect.
- `Derived` mode shows controls for the base color and for each supported transformation: `rotate_hue`, `mix`, and `alpha`.
- The base color selector offers the standard active-theme token identifiers: `background`, `foreground`, `accent`, and `guide`.
- Each transformation has an enable switch. Disabled transformations are omitted from the saved color expression.
- Enabled transformations are shown and saved in the fixed operation order: `rotate_hue`, then `mix`, then `alpha`.
- `rotate_hue` exposes a signed degree value edit and matching slider.
- `mix` exposes an amount value edit and matching slider from `0.0` to `1.0`; its target color selector appears on a separate labeled row and offers the same active-theme token identifiers.
- `alpha` exposes an alpha byte value edit and matching slider from `0x00` to `0xFF`.
- Valid edits preview immediately. Invalid or incomplete derived expressions do not preview and do not replace the last valid pending value.
- Per-field revert restores the whole color role expression, including its mode and all transformation controls.
- The window displays the effective resolved color preview for both literal and derived roles. Derived roles also show the resolved `#RRGGBBAA` value as read-only text next to the preview swatch.

`[layout_guide_sheet]` remains diagnostics-only, is consumed by `CaseDashHeadless.exe` from its separate embedded resource, and is not editable through the dialog.
