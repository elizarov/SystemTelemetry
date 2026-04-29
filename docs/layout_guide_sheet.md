# Layout Guide Sheet

This document owns the condensed feature spec for the diagnostics layout guide sheet.
See also: [docs/diagnostics.md](diagnostics.md) for diagnostics CLI behavior, [docs/specifications.md](specifications.md) for live layout-edit behavior, and [docs/layout.md](layout.md) for config language ownership.

## Purpose And Scope

The layout guide sheet is a diagnostics screenshot artifact that explains the editable geometry of a configured dashboard layout on one inspection-friendly page.

The sheet renders a representative subset of cards from the selected layout, surrounds them with the same layout-edit visual guides used by the application, and connects each editable target to tooltip-like help text outside the card area. The artifact is intended for configuration authors, localization review, and diagnostics inspection.

The sheet is not an alternate dashboard layout, does not change the live configuration, and does not replace the ordinary dashboard screenshot export.

## Name

The feature name is `Layout Guide Sheet`.

User-visible labels and documentation refer to the generated image as a `layout guide sheet`. Command-line names, filenames, trace event names, and internal identifiers use `layout-guide-sheet` or `layout_guide_sheet` consistently with the surrounding naming convention.

## Input Selection

- The sheet uses the same loaded configuration, selected named layout, scale override, default-config behavior, fake-data behavior, and localization catalog as the diagnostics run.
- The active named layout is selected by normal config resolution or by `/layout:<name>`.
- The content source is the latest diagnostics snapshot. Fake telemetry is preferred for repeatable output, but the sheet accepts live telemetry when the diagnostics run uses live data.
- Blank rendering mode is not part of the guide-sheet contract. The sheet exists to explain editable UI and therefore renders representative content, guides, anchors, callouts, and help text.
- The guide-sheet generator works from the resolved layout model rather than parsing `config.ini` text directly.

## Card Coverage

- The sheet includes the smallest practical subset of cards from the selected layout that shows every widget type present in that layout at least once.
- When multiple cards contain the same widget type, selection favors the card that covers the most not-yet-covered widget types.
- Ties favor the card order from the selected layout.
- A reused card layout counts as covering the widgets it renders in the selected layout.
- Empty cards and cards that contain only already-covered widget types are omitted unless no non-empty card exists.
- If the selected layout cannot produce any card content, the sheet renders a diagnostics error panel that names the selected layout and explains that no representative cards are available.
- The sheet never invents cards or widgets that are absent from the selected layout. If a widget type is not present, it is not shown.

## Sheet Layout

- The output is a single PNG image.
- Cards are arranged vertically in layout order after coverage selection.
- Each card keeps its resolved card size, inner composition, widget drawing, card chrome, colors, fonts, and configured scale.
- The sheet adds generous whitespace between cards so visual guides, anchors, rulers, and callouts do not collide with neighboring cards.
- The card column is placed away from the image edges so callouts can fit around cards without clipping.
- The sheet canvas grows to fit selected cards and callouts instead of scaling card content down.
- The generated image uses a transparent or diagnostics-neutral background only when that matches existing screenshot-export conventions; otherwise it uses the normal dashboard background.
- Output dimensions are deterministic for the same config, layout, scale, localization, and data source.

## Visual Guides

- Each selected card renders with layout-edit mode enabled.
- The sheet shows all visual guides, anchors, size rulers, hover-equivalent outlines, text anchors, color targets, gap handles, split guides, reorder handles, widget-local guides, and card or dashboard spacing guides that are available for the selected card content.
- Guide geometry comes from the same renderer-owned layout-edit artifact registration path used by live layout-edit mode and diagnostics active-region tracing.
- Guide rendering remains visually consistent with live layout-edit rendering, including stroke weights, colors, dash patterns, anchor shapes, ruler numbering, and selected or hovered emphasis.
- The sheet may force normally hover-only guide families visible at the same time when that is necessary to explain all editable targets.
- Representative guides and anchors are not promoted to active or selected styling just because a callout points at them; the panel content renders as it would in the corresponding layout-edit hover state.
- When two guides overlap, the sheet follows the live hit-priority and visual-priority rules unless the callout placement needs a small leader-line offset to remain readable.

## Callouts

- Each documented editable kind appears in exactly one help bubble.
- Each help bubble has exactly one thin straight leader line connected to one representative target area for that kind.
- When the same kind appears multiple times, the representative target area is the topmost example in sheet coordinates, with leftmost position breaking ties.
- Leader lines use the shortest direct segment from the selected active-area attachment point to the selected bubble attachment point.
- Leader lines use a subtle one-pixel stroke and avoid overpowering the actual guides.
- Help bubbles visually match tooltip styling closely enough that the sheet can be used to review localized tooltip text at a glance.
- Help bubbles have two text lines:
  - Line 1 shows the config parameter, field, or edit target shape controlled by the guide.
  - Line 2 shows the same localized description text used by the live layout-edit tooltip for that target.
- The first line uses the same wording and value shape that the live tooltip uses for the edited config target.
- The second line comes from the shared tooltip-description source of truth, not from guide-sheet-only strings.
- Help bubbles are placed outside the card bounds with enough margin to keep the card content unobscured.
- Callouts may be arranged to the left, right, above, or below each card.
- Callout placement is deterministic and collision-aware. Bubbles do not overlap other bubbles, cards, or important guide labels.
- Repeated examples of the same kind remain visible as ordinary guides, but they do not get additional bubbles or leader lines.
- If the sheet cannot place every bubble without overlap, it prefers preserving text readability and emits a trace warning that names the affected card, documented kind, and representative target.

## Callout Visual Style

- Callout styling uses the active renderer palette so guide-sheet colors follow the loaded config.
- The guide sheet introduces dedicated palette entries for callout-only chrome: `layout_guide_callout_leader_color`, `layout_guide_callout_fill_color`, `layout_guide_callout_border_color`, `layout_guide_callout_parameter_color`, and `layout_guide_callout_description_color`.
- With the shipped palette, panel guide chrome uses its normal layout-edit hover colors, leader lines use `layout_guide_callout_leader_color` (`#FFFF00D9`), bubble fill uses `layout_guide_callout_fill_color` (`#06080BF5`), bubble border uses `layout_guide_callout_border_color` (`#1E2837FF`), parameter text uses `layout_guide_callout_parameter_color` (`#FFFFFFFF`), and description text uses `layout_guide_callout_description_color` (`#A5B4BEFF`).
- Only callout-specific chrome, meaning leader lines and help bubbles, receives distinct guide-sheet styling.
- Guide-sheet callout colors come directly from those palette entries rather than from adjusted variants of other colors.
- Leader lines are one physical pixel wide after scale conversion, solid, square-capped, and drawn above card content but below bubble borders and text.
- Leader lines use `layout_guide_callout_leader_color` exactly as configured.
- The leader starts at the closest point on the representative target rectangle or anchor shape and ends at the closest point on the bubble border.
- The leader does not use arrowheads, dots, elbows, curves, glow, or shadow.
- Bubbles use a rounded rectangle with 4 logical pixels of corner radius, 8 logical pixels of horizontal padding, 6 logical pixels of vertical padding, and a 1 logical pixel solid border.
- Bubble fill uses `layout_guide_callout_fill_color` exactly as configured.
- Bubble borders use `layout_guide_callout_border_color`.
- Bubble text uses the configured `small` font role. The first line uses the role's configured weight or semibold if the role is regular; the second line uses the role's configured weight.
- The first line uses `layout_guide_callout_parameter_color`; the second line uses `layout_guide_callout_description_color`.
- A 3 logical pixel gap separates the parameter line from the description line.
- Wrapped continuation lines keep the same color as their source line and align to the bubble text inset.
- Bubbles do not use drop shadows or blurred backplates because the artifact must stay crisp under screenshot comparison.

## Bubble Layout Algorithm

The guide-sheet bubble layout is deterministic and runs after the selected cards have resolved their normal dashboard geometry and layout-edit active areas.

1. Collect targets. Build one candidate record for each editable active area that should be documented. Each record stores its card id, active-area rectangle, guide kind, tooltip parameter line, tooltip description line, priority, and stable order from the active-region or edit-artifact source.
2. Select representatives. Group records by documented kind. Each kind produces exactly one bubble. The bubble's representative target area is the topmost record in sheet coordinates, with leftmost position and then stable source order breaking ties. Non-representative records keep their guides but do not receive bubbles or leaders.
3. Measure bubbles. Format each bubble using the active tooltip text source, fixed padding, a minimum width, and a maximum width. Measure wrapped text before placement so every candidate rectangle has stable dimensions.
4. Build candidate slots. For each bubble, generate candidate rectangles around its card in four zones: right, left, below, and above. Candidate rows or columns are aligned to the target center first, then expanded in stable increments away from occupied space. The candidate list is ordered by zone preference, distance from target, and stable target order.
5. Score candidates. Reject candidates that overlap card rectangles, already placed bubbles, sheet margins, or reserved guide-label rectangles. Score the remaining candidates by leader length, zone balance, distance from the target center, and whether the straight leader line intersects another bubble.
6. Place bubbles. Iterate bubbles by priority and stable representative order. Pick the lowest-scoring candidate. If no candidate fits, grow the sheet canvas in the needed direction, rebuild that bubble's candidate slots, and retry once before falling back to the least-overlapping candidate.
7. Assign attachment points. For each placed bubble, choose the representative target edge point and bubble edge point that produce the shortest straight leader segment.
8. Compact zones. After initial placement, compact bubbles within each placement zone toward their cards while preserving order and non-overlap. Recompute attachment points after compaction.
9. Emit diagnostics. Trace the selected card ids, covered widget types, documented kind count, bubble count, representative target ids, canvas size, and every fallback placement warning.

The algorithm favors readable bubbles and deterministic output over perfect leader-line routing. It does not attempt obstacle-avoiding polylines because the sheet uses straight leaders as part of its visual language.

## Text And Localization

- The sheet uses the active localization catalog for tooltip descriptions, menu-independent labels, and diagnostic error text.
- Tooltip help text is rendered with the same font family and comparable sizing as live tooltips, adjusted only as needed for screenshot readability.
- Bubbles allow wider text than live Win32 tooltips when needed to keep the sheet readable, but the two-line tooltip structure remains visible.
- Long first-line parameter shapes may wrap only at stable separators such as dots, brackets, commas, or parentheses.
- Long second-line descriptions may wrap inside the bubble, but the first visible line of the description remains directly below the parameter line.
- Missing localization falls back through the same fallback path as live tooltip rendering and is visible in the sheet so localization gaps are reviewable.

## Diagnostics Behavior

- The layout guide sheet is requested explicitly with `/layout-guide-sheet[:path]` and does not appear as a side effect of `/screenshot`.
- A headless `/exit` run that requests a guide sheet exports it once and exits.
- A UI-attached run that requests a guide sheet refreshes it on the same cadence as other diagnostics screenshots unless the implementation defines a cheaper explicit-refresh policy in diagnostics docs.
- Explicit output paths resolve by the same current-working-directory rules as other diagnostics outputs.
- The default output filename is `telemetry_layout_guide_sheet.png`.
- The output overwrite behavior matches ordinary screenshot export.
- When `/trace` is enabled, guide-sheet export writes a start event, an end event, the selected layout name, selected card ids, covered widget types, output dimensions, placed callout count, and any callout-placement warnings.
- Guide-sheet trace events use `diagnostics:layout_guide_sheet` as the event family.
- Failures follow the diagnostics failure policy: traced runs report the failure to trace and return a failure exit code in headless mode rather than blocking behind modal UI.

## Non-Goals

- The sheet does not generate a complete printed manual.
- The sheet does not show widgets that are not present in the selected layout.
- The sheet does not edit, save, normalize, or reformat config files.
- The sheet does not replace the live `Edit Configuration` dialog.
- The sheet does not define new config parameters; it documents the editable parameters already exposed by layout-edit metadata.
