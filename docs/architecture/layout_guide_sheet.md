# Layout Guide Sheet Package

`src/layout_guide_sheet/` owns diagnostics layout guide sheet generation from resolved layout data. It exposes a package-root pipeline API and keeps planner, renderer, callout request types, placement, and private helper modules under `src/layout_guide_sheet/impl/`.

## Responsibilities

- Collect packed-overview active regions from the resolved layout model.
- Select representative cards and callout candidates that cover visible widget types.
- Group equivalent metric-definition rows into representative hovered rows.
- Plan overview callouts, draw-free callout side geometry, leader routing, and final sheet rendering.
- Place callout stacks, promote top and bottom callouts, score leaders, repair side splits, and expose placement trace data.
- Report benchmark stages as `active_regions`, `sheet_plan`, `sheet_measure`, `sheet_place`, and `sheet_draw`.
- Resolve layout guide sheet color expressions from active theme tokens and resolved `[colors]` roles.
- Preserve widget-owned edit affordances through dashboard-renderer guide-sheet support linked only by headless, tests, and benchmarks.

## Boundaries

- `headless` invokes the pipeline through the diagnostics output-handler boundary. Diagnostics owns export command handling, path resolution, and trace records without depending on this package.
- `dashboard_renderer` exposes only the resolved-card summaries, card-chrome artifact hooks, and rendering hooks that the `layout_guide_sheet` package needs; their implementations are kept in a guide-sheet support source file that the shipped app does not link.
- Layout guide sheet styling uses `LayoutGuideSheetConfig` and a separate renderer palette path rather than adding edit-dialog color targets for diagnostics-only colors.
