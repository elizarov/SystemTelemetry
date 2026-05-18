# Layout Guide Sheet

This document owns the condensed feature spec for the diagnostics layout guide sheet.
See also: [docs/diagnostics.md](diagnostics.md) for diagnostics CLI behavior, [docs/layout_edit.md](layout_edit.md) for live layout-edit behavior, and [docs/layout.md](layout.md) for config language ownership.

## Purpose And Scope

The layout guide sheet is a diagnostics screenshot artifact that explains the editable geometry of a configured dashboard layout on one inspection-friendly page.

The sheet renders a compact overview of the selected layout plus a representative subset of cards, surrounds them with the same layout-edit visual guides used by the application, and connects each editable target to tooltip-like help text outside the rendered area. The artifact is intended for configuration authors, localization review, and diagnostics inspection.

The sheet is not an alternate dashboard layout, does not change the live configuration, and does not replace the ordinary dashboard screenshot export.

## Name

The feature name is `Layout Guide Sheet`.

User-visible labels and documentation refer to the generated image as a `layout guide sheet`. Command-line names, filenames, trace event names, and internal identifiers use `layout-guide-sheet` or `layout_guide_sheet` consistently with the surrounding naming convention.

## Input Selection

- The sheet uses the same loaded configuration, selected named layout, scale override, default-config behavior, fake-runtime behavior, and localization catalog as the diagnostics run.
- The active named layout is selected by normal config resolution or by `/layout:<name>`.
- The content source is the latest diagnostics snapshot. Built-in synthetic telemetry is preferred for repeatable output, but the sheet accepts live telemetry when the diagnostics run uses live data.
- Blank rendering mode is not part of the layout guide sheet contract. The sheet exists to explain editable UI and therefore renders representative content, guides, anchors, callouts, and help text.
- The layout guide sheet generator works from the resolved layout model rather than parsing `config.ini` text directly.

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
- A compact screen overview is rendered before the representative cards.
- The overview includes every card in the selected layout, preserves the row/column topology, configured between-card spacing, dashboard padding, titles, and icons, and omits card contents.
- The overview measures each card at the same scale as the representative cards, using only the horizontal space needed for the card icon and title plus card padding; it does not reserve vertical content space, does not apply layout weights during this intrinsic-size pass, and does not scale down the resolved screen layout.
- The overview is drawn inside a packed screen box that represents the dashboard bounds without using the dashboard's full window size. After the packed size is known, the overview performs one weighted relayout inside that packed box so extra row or column space from the compact topology follows the configured layout weights.
- Overview dashboard guides, gap handles, reorder handles, and card-chrome hover targets are collected as active regions from the packed overview geometry itself, so callout selection and visible positions match the compact cards rather than the full-size dashboard coordinates.
- Overview dashboard spacing callouts render their full hover-equivalent sizing guide line, caps, and handle against the packed overview geometry.
- Packed overview cards render their chrome through the same card-chrome widget layout, draw, guide, and anchor registration path as normal dashboard cards.
- The overview and each representative card are arranged as separate vertical sheet blocks in layout order.
- Each sheet block owns its own local callout placement. Most callouts sit in snug left and right stacks beside that block's annotated overview or card instead of sharing page-wide callout columns.
- Completed sheet blocks are centered horizontally on the page after their local callout stacks, annotated item, and leader geometry are measured.
- Cards are arranged vertically in layout order after coverage selection.
- Each card keeps its resolved card size, inner composition, widget drawing, card chrome, colors, fonts, and configured scale.
- The sheet adds generous whitespace between cards so visual guides, anchors, rulers, and callouts do not collide with neighboring cards.
- The card column is placed away from the image edges so callouts can fit around cards without clipping.
- The sheet canvas grows to fit selected cards and callouts instead of scaling card content down.
- The generated image uses a transparent or diagnostics-neutral background only when that matches existing screenshot-export conventions; otherwise it uses the normal dashboard background.
- Output dimensions are deterministic for the same config, layout, scale, localization, and data source.

## Visual Guides

- Each selected card renders with layout-edit mode enabled.
- The overview shows dashboard-level card spacing guides, dashboard padding controls, and card-chrome controls such as title, icon, border, radius, padding, and header spacing.
- The overview shows one left/right and one up/down layout-child reorder handle with callouts.
- Representative cards show visual guides, anchors, size rulers, hover-equivalent outlines, text anchors, color targets, gap handles, split guides, reorder handles, and widget-local guides that are available for the selected card content.
- Guide geometry comes from the same renderer-owned layout-edit artifact registration path used by live layout-edit mode and diagnostics active-region tracing.
- Guide rendering remains visually consistent with live layout-edit hover rendering, including stroke weights, colors, dash patterns, anchor shapes, and ruler numbering.
- The sheet may force normally hover-only guide families visible at the same time when that is necessary to explain all editable targets.
- Forced layout guide sheet affordances always use hover-equivalent visual styling, including color, line width, dash pattern, and emphasis; dialog-active or selected `active_edit_color` styling is not used on the sheet.
- Every callout target renders the same hover-equivalent affordance that live layout-edit mode shows for that target family, including widget-local gauge and metric-list guides, card-internal layout guides, gap handles, text anchors, and color targets.
- Overview card title and icon callouts render the corresponding packed card chrome as hovered, including the exact dotted text or icon target outline and the matching anchor shape produced by card-chrome edit-artifact registration.
- Overview color callouts attach to the same packed card-chrome active text or icon region that live layout-edit mode uses, and color-only callouts do not draw an extra target outline.
- Overview anchor callouts attach their leader to the visible anchor shape. Anchors that have no live hover target outline do not draw an extra target outline.
- Representative-card anchor callouts attach their leaders to the resolved drag-anchor rectangle rather than the broader widget or target region. Circular radial-distance anchors attach to the closest point on the resolved circle.
- Representative-card gauge accent and track color callouts attach to the corresponding left or right midpoint of the visible ring stroke rather than the center of the broad color hit rectangle.
- Representative guides and anchors are not promoted to active or selected styling just because a callout points at them; the panel content renders as it would in the corresponding layout-edit hover state.
- Representative cards render hover-equivalent state only for active areas that have callouts on that representative card. Card-level or dashboard-level gap and sizing controls that are documented in the overview do not render in hover state on the representative cards.
- Metric-list layout reorder callouts appear once per metric-list widget, attached to the first visible row, rather than repeating the same layout guidance for each row.
- Representative-card layout sizing guide callouts render the guide line itself without the full containing-layout dashed outline to keep the specimen readable.
- Representative-card anchor callouts keep the same widget-hover context as live hover for related artifact selection, so active-only target outlines do not replace the normal thin widget hover outline around controls such as gauge segment-count anchors.
- When two guides overlap, the sheet follows the live hit-priority and visual-priority rules unless the callout placement needs a small leader-line offset to remain readable.

## Callouts

- Each documented tooltip text for an active area inside the overview or a rendered representative card appears in at least one help bubble.
- Dashboard-level layout guides, between-card gaps, dashboard outer padding, and card-chrome controls produce callouts in the overview.
- Overview dashboard sizing guides produce at most one horizontal-guide callout and at most one vertical-guide callout, using the first representative of each orientation in screen order.
- Overview layout-child reorder handles produce at most one left/right callout and at most one up/down callout, using the first representative of each orientation in screen order.
- Representative cards do not repeat overview-owned card title, icon, card style, dashboard spacing, or dashboard padding callouts.
- Color-parameter callouts are unique across the whole sheet. If an overview callout already documents a color parameter such as `icon_color`, representative cards keep their visible color target affordances but do not add another bubble or leader for that same color parameter.
- Metric-definition rows under `[metrics]`, such as `cpu.temp`, `cpu.clock`, and other metric ids, are equivalent for layout guide sheet coverage. The first rendered metric-definition row receives the single representative bubble and leader, and that row renders the same hover-equivalent dotted outline, wedge anchor, and text-size anchor that live layout-edit mode shows for the hovered metric row.
- Each help bubble has exactly one thin straight leader line connected to one representative target area for that tooltip text.
- When the same tooltip text appears multiple times, the representative target area is the topmost example in sheet coordinates, with leftmost position breaking ties.
- Leader lines use the direct segment from the representative target point to the selected bubble attachment point.
- Leader lines on the same side of the same card must not intersect other leader lines; callout placement validates each side stack before drawing.
- Leader lines use a subtle one-pixel stroke and avoid overpowering the actual guides.
- Help bubbles visually match tooltip styling closely enough that the sheet can be used to review localized tooltip text at a glance.
- Help bubbles have two text lines:
  - Line 1 shows the config parameter, field, or edit target shape controlled by the guide.
  - Line 2 shows the same localized description text used by the live layout-edit tooltip for that target.
- The first line uses the same wording and value shape that the live tooltip uses for the edited config target.
- The second line comes from the shared tooltip-description source of truth, not from separate layout guide sheet strings.
- Help bubbles are placed outside the card bounds with enough margin to keep the card content unobscured.
- Callouts are arranged mostly to the left or right of each card. After the side split is known, one left-stack item may move below the annotated item and one right-stack item may move above it, giving each block one top and one bottom callout centered against the annotated item when those side stacks exist.
- Promoted top and bottom callouts keep their natural single-line width unless that width would overlap a left or right callout stack. Only in that case, the promoted callout is constrained to the annotated item width and its description line wraps vertically.
- Callout placement is deterministic and collision-aware. Bubbles do not overlap other bubbles, cards, or important guide labels.
- Repeated examples of the same kind remain visible as ordinary guides, but they do not get additional bubbles or leader lines. Widget-content guides for cards that are not rendered as representative cards do not produce callouts.
- If the sheet cannot place every bubble without overlap, it prefers preserving text readability and emits a trace warning that names the affected card, documented kind, and representative target.

## Callout Visual Style

- Callout styling uses the active renderer palette so layout guide sheet colors follow the loaded config.
- The layout guide sheet uses dedicated `[layout_guide_sheet]` entries for callout-only chrome: `callout_leader_color`, `callout_fill_color`, `callout_border_color`, `callout_parameter_color`, and `callout_description_color`.
- With the shipped palette, panel guide chrome uses its normal layout-edit hover colors, leader lines use `callout_leader_color` (`#FFE45CE6`), bubble fill uses `callout_fill_color` (`#06080BF5`), bubble border uses `callout_border_color` (`#B88A22FF`), parameter text uses `callout_parameter_color` (`#FFFFFFFF`), and description text uses `callout_description_color` (`#A5B4BEFF`).
- Only callout-specific chrome, meaning leader lines and help bubbles, receives distinct layout guide sheet styling.
- Layout guide sheet callout colors come directly from those `[layout_guide_sheet]` entries rather than from adjusted variants of other colors, and they are not exposed in the layout-edit dialog.
- Leader lines use the configured `leader_stroke_width`, are solid and square-capped, and are drawn above card content but below bubble borders and text.
- Leader lines use `callout_leader_color` exactly as configured.
- The leader starts at the representative target rectangle or anchor shape and ends at the closest compatible point on the bubble border.
- A small filled circle using `callout_leader_color` marks the bubble-border attachment point. Its diameter comes from `leader_endpoint_diameter`.
- The leader does not use arrowheads, elbows, curves, glow, or shadow.
- Bubbles use a rounded rectangle with configured corner radius, horizontal padding, vertical padding, and border width.
- Bubble fill uses `callout_fill_color` exactly as configured.
- Bubble borders use `callout_border_color`.
- Bubble text uses the configured `small` font role. The first line uses the role's configured weight or semibold if the role is regular; the second line uses the role's configured weight.
- The first line uses `callout_parameter_color` and matches the live layout-edit tooltip text, including configured color expressions for color targets; the second line uses `callout_description_color` and is always reserved when the tooltip has description text.
- The configured `callout_line_gap` separates the parameter line from the description line.
- Bubbles do not use drop shadows or blurred backplates because the artifact must stay crisp under screenshot comparison.
- Layout guide sheet margins, block spacing, callout spacing, bubble padding, border widths, leader stroke and endpoint sizes, and packed-overview helper stroke and handle sizes come from `[layout_guide_sheet]`.

## Bubble Layout Algorithm

The layout guide sheet bubble layout is deterministic and runs after the selected cards have resolved their normal dashboard geometry and layout-edit active areas.

1. Collect targets. Compose the packed overview without widget content, collect its real tooltip-bearing active regions, and separately collect editable active areas that overlap a rendered representative card. Each record stores its rendered source id, active-area rectangle, guide kind, tooltip parameter line, tooltip description line, hover-equivalent overlay state, priority, and stable order from the active-region or edit-artifact source.
2. Select representatives. Group records by distinct tooltip text, except metric-definition records group together as one equivalent class, color records group by edited color parameter across the whole sheet, and card-title records group together as one overview class. Each group produces one bubble. The bubble's representative target area is the topmost record in sheet coordinates, with leftmost position and then stable source order breaking ties. Non-representative records keep their guides but do not receive bubbles or leaders.
3. Measure bubbles. Format each bubble using the active tooltip text source and fixed padding. Measure both tooltip lines as single-line text and size the bubble to the widest measured line plus padding, without minimum or maximum width clamping.
4. Split sides. For each card, sort representative targets by target-center x and split them near the median so the left half uses the left stack and the right half uses the right stack. A single target uses the side nearest its target-center x. This split is the optimizer's initial guess, not a hard constraint.
5. Order stacks. Sort each side stack by representative target-center y. This gives the optimizer a stable readable starting order before the leaders are drawn.
6. Promote top and bottom callouts. Move the lowest left-stack callout to the bottom slot and the highest right-stack callout to the top slot when those stacks have callouts. These promoted slots are preserved during later optimization by exchanging promoted callouts with side-stack replacements rather than dropping the slot.
7. Repair conflicts. Score the current straight leaders, collect the specific leader pairs that cross or pass through another target's safe zone, and generate mutations from only those problematic callouts. Candidate mutations include swapping the two conflicting callouts directly, swapping either conflicted callout with its current stack neighbors, swapping either conflicted callout with another placed callout, and moving a conflicted side callout to another side-stack position. For promoted top or bottom callouts, relocation swaps in a side-stack replacement so the promoted slot remains occupied. The optimizer applies the best score-improving mutation, then repeats until no mutation improves the score or the bounded pass limit is reached.
8. Place bubbles. Center each remaining left and right stack on the annotated item center y inside that item's local sheet block. A tall stack may start above the item top and extend below the item bottom by roughly matching amounts. Place top and bottom bubbles above and below the annotated item, centered against the annotated item box.
9. Fit promoted bubbles. If a centered top or bottom bubble overlaps the left or right callout stack, constrain only that promoted bubble to the annotated item width and wrap its description line while keeping its parameter line single-line.
10. Assign attachment points. Within each sheet block, align left bubbles by their right edge and right bubbles by their left edge so all leader endpoints on a side share the same x coordinate. Side leaders attach to the vertical center of the nearest bubble edge. Top and bottom leaders attach to the center of the nearest horizontal bubble edge and run diagonally to the representative target point when the target is off center. Each leader starts at the representative target point and ends at the matching bubble edge.
11. Preserve optimized order. After placement, keep the scored stack order intact so a narrower same-side cleanup pass cannot introduce new top, bottom, or cross-stack leader conflicts.
12. Compose blocks. Measure each annotated item as the center of a local bounding box that contains the side stacks plus any promoted top and bottom bubbles at their actual positions. Small side stacks let top and bottom bubbles define the vertical extent; tall side stacks define the vertical extent when they protrude farther than the promoted bubbles. Completed blocks are then placed down the page with the configured inter-block spacing.
13. Compact zones. Recompute attachment points after any order adjustment while preserving non-overlap and side alignment.
14. Emit diagnostics. Trace the selected card ids, covered widget types, documented kind count, bubble count, representative target ids, per-card leader score, conflict-repair pass count, canvas size, and every remaining placement intersection.

The algorithm favors readable bubbles and deterministic output while preserving non-intersecting straight leader lines. Leader optimization treats each representative target point as a small keep-clear zone so unrelated leaders do not visually pass through another target marker. It does not attempt obstacle-avoiding polylines because the sheet uses straight leaders as part of its visual language.

## Text And Localization

- The sheet uses the active localization catalog for tooltip descriptions, menu-independent labels, and diagnostic error text.
- Tooltip help text is rendered with the same font family and comparable sizing as live tooltips, adjusted only as needed for screenshot readability.
- Bubbles allow wider text than live Win32 tooltips when needed to keep the sheet readable, but the two-line tooltip structure remains visible.
- Long first-line parameter shapes stay on one header line.
- Long second-line descriptions stay on one line like the parameter line.
- Missing localization falls back through the same fallback path as live tooltip rendering and is visible in the sheet so localization gaps are reviewable.

## Diagnostics Behavior

- The layout guide sheet is requested explicitly with `/layout-guide-sheet[:path]` and does not appear as a side effect of `/screenshot`.
- A headless `/exit` run that requests a layout guide sheet exports it once and exits.
- A UI-attached run that requests a layout guide sheet refreshes it on the same cadence as other diagnostics screenshots unless the implementation defines a cheaper explicit-refresh policy in diagnostics docs.
- Explicit output paths resolve by the same current-working-directory rules as other diagnostics outputs.
- The default output filename is `casedash_layout_guide_sheet.png`.
- The output overwrite behavior matches ordinary screenshot export.
- When `/trace` is enabled, layout guide sheet export writes a start event, an end event, the selected layout name, selected card ids, covered widget types, output dimensions, placed callout count, and any callout-placement warnings.
- Guide-sheet trace events use `diagnostics:layout_guide_sheet` as the event family.
- Failures follow the diagnostics failure policy: traced runs report the failure to trace and return a failure exit code in headless mode rather than blocking behind modal UI.

## Non-Goals

- The sheet does not generate a complete printed manual.
- The sheet does not show widgets that are not present in the selected layout.
- The sheet does not edit, save, normalize, or reformat config files.
- The sheet does not replace the live `Edit Configuration` window.
- The sheet does not define new config parameters; it documents the editable parameters already exposed by layout-edit metadata.
