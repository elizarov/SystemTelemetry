# Layout Guide Sheet Package

`src/layout_guide_sheet/` owns diagnostics guide-sheet generation from resolved layout data. It exposes a package-root pipeline API and keeps planner, renderer, callout request types, placement, and private helper modules under `src/layout_guide_sheet/impl/`.

## Responsibilities

- Collect packed-overview active regions from the resolved layout model.
- Select representative cards and callout candidates that cover visible widget types.
- Group equivalent metric-definition rows into representative hovered rows.
- Plan overview callouts, draw-free callout side geometry, leader routing, and final sheet rendering.
- Place callout stacks, promote top and bottom callouts, score leaders, repair side splits, and expose placement trace data.
- Report benchmark stages as `active_regions`, `sheet_plan`, `sheet_measure`, `sheet_place`, and `sheet_draw`.
- Preserve widget-owned edit affordances through the dashboard renderer's layout-guide-sheet render mode.

## Boundaries

- Diagnostics invokes the pipeline and owns export command handling and trace records.
- `dashboard_renderer` exposes only the resolved-card summaries, card-chrome artifact hooks, and rendering hooks that the guide-sheet package needs.
- Guide-sheet styling uses `LayoutGuideSheetConfig` and a separate renderer palette path rather than adding edit-dialog color targets for diagnostics-only colors.
