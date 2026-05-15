# Dashboard Animation

This document owns the target live-dashboard animation behavior and implementation architecture.
See also: [docs/specifications.md](specifications.md) for current user-visible dashboard behavior, [docs/layout_edit.md](layout_edit.md) for layout-edit interaction, [docs/diagnostics.md](diagnostics.md) for deterministic render exports, [docs/profile_benchmark.md](profile_benchmark.md) for renderer performance baselines, and [docs/architecture.md](architecture.md) for package boundaries.

## Purpose

The live dashboard animates visual metric indicators between telemetry snapshots so values feel continuous across the 0.5 second telemetry cadence while keeping idle CPU usage low.

Animation is scoped to data-driven visuals:

- Metric-list pill-bar fill and recent-peak marker.
- Drive-usage pill-bar fill and read/write stacked activity fill.
- Gauge value fill and recent-peak indicator.
- Throughput chart plot, leader position, time-marker phase, and vertical scale.

The implementation does not animate text, card chrome, widget labels, layout geometry, or edit affordance strokes. Text continues to update only when a new telemetry snapshot is accepted.

## Current Architecture Fit

The current code already provides several pieces the animation design depends on:

- `TelemetryRuntime` owns the 500 ms worker cadence and publishes copied `TelemetryUpdate` objects to `DashboardApp`.
- `DashboardApp::EnqueueTelemetryUpdate()` already uses an overwrite-style main-thread handoff: a newer pending update replaces the older one before the UI thread drains it.
- `DashboardRenderer` owns dashboard layout resolution, widget traversal, renderer style selection, metric-source caching, and layout-edit artifact collection.
- Widget implementations already separate geometry resolution from drawing through `ResolveLayoutState()` and `Draw()`.
- Animated visuals are concentrated in a small set of widget helpers: `DrawWidgetPillBar()` in `widget/impl/pill_bar.*`, gauge arc drawing in `widget/impl/gauge.*`, drive activity and usage drawing in `widget/impl/drive_usage_list.*`, and throughput graph drawing in `widget/impl/throughput.*`.
- `MetricSource` already exposes normalized scalar ratios, recent-peak ratios, smoothed throughput histories, shared throughput graph maxima, and time-marker offsets.
- The live renderer and screenshot renderer use the same Direct2D and DirectWrite scene, so deterministic diagnostics rendering can keep reusing the existing immediate draw path.

The current implementation adds the shared animation cadence, public animation identity and opaque state interfaces, widget-private animation state implementations, widget-host animation submission, deterministic offscreen bypass, snapshot/overlay layer bitmap construction, and a package-private render-thread presenter:

- The live window draw path is split: `DashboardApp::Paint()` calls `DashboardRenderer::DrawWindow()`, the main thread paints snapshot and optional overlay layers into renderer-owned bitmap resources, collects immutable widget animation objects, and publishes the newest complete frame to `DashboardRenderThread`.
- `DashboardRenderThread` owns the HWND presenter renderer, the keyed `DashboardAnimationTimeline`, an overwrite-only mailbox, surface-version handling, and the animation frame loop. It composes snapshot bitmap, snapshot animations, optional overlay bitmap, and overlay animations on the presenter thread.
- Snapshot and overlay layer bitmaps are acquired from a dashboard-renderer pool and returned after the render thread replaces or discards the frame that owns them. The benchmark immediate-present path uses the same acquire/release rule while staying single-threaded, so paint benchmarks measure layer bitmap construction and final composition without scheduler noise.
- `D2DRenderer` exposes generic layer bitmap drawing, bitmap-region drawing, bitmap composition, and retained dirty-window composition. The live presenter currently uses the existing Direct2D HWND render target behind the render-thread boundary; replacing that target with the planned DXGI flip-model swap chain remains a renderer-backend step.
- Widgets draw snapshot text and tracks while submitting widget-owned animation objects tagged with the current dashboard layer. Widget overlay hooks submit overlay-tagged animations for content that moves above the base dashboard during layout editing.
- Layout-edit dragged-child replay happens by re-entering widget draw code in the overlay pass under the drag translation. Its widget animations use the overlay tag and the render-thread timeline.

## Target Behavior

On every accepted telemetry snapshot, the main thread updates static text immediately and publishes new animation target values. The first presented frame after that snapshot shows the new text with the animated visuals still at the previous visual value. The visuals interpolate to the new target over the next 500 ms.

If a newer telemetry snapshot arrives before the previous 500 ms interpolation finishes, the render thread samples the current interpolated value at the arrival time and uses that sampled value as the start value for the new interpolation. Visual motion stays continuous.

When an animation key appears without previous render-thread data, the render thread creates a zero-initialized start value. This applies on application startup and when a metric first appears after a layout, binding, row-order, or drive-selection change.

When a layout edit changes widget geometry or row order but keeps the same logical metric key visible, the previous interpolated state remains stored by key and the next animation target reuses it. Geometry belongs to the widget animation draw object, not to the stored animation state, so layout edits move the drawn animation without restarting its value interpolation.

When a metric target becomes unavailable, permission-gated, or otherwise not drawable, the text updates immediately while the visual animates from its current value to zero. The visual disappears after the zero-value animation completes.

Animation runs only while at least one active animation is in progress or while the main thread publishes layer updates. After all animations reach their targets, the render thread presents the final frame and waits without running a frame loop.

The animation duration is the telemetry refresh cadence. Production code exposes one shared 500 ms cadence constant and uses it for both telemetry collection scheduling and dashboard animation duration.

Blank render mode, screenshot exports, layout-guide-sheet exports, app-icon exports, unit tests, and other deterministic offscreen renders do not use live interpolation. They render the target snapshot values directly through the deterministic draw path. UI-attached diagnostics screenshots also render target values rather than the currently interpolated live frame.

## Layer Model

The dashboard frame is drawn in two ordered layers. Each layer has its own immutable widget animation draw list, and both animation lists share the same keyed timeline.

### Snapshot Layer

The snapshot layer is opaque. It contains:

- Dashboard background, card chrome, headers, static labels, and all text.
- Animated-widget static backgrounds, including pill-bar tracks, gauge track segments, chart background, chart axes, and non-animated chart labels.
- Static content for the current layout-edit drag state when that content belongs below animated fills.

Widgets draw snapshot content through `Widget::Draw()` and submit `WidgetAnimationLayer::Snapshot` animations through `WidgetHost::AddWidgetAnimation()`. The dashboard renderer records those animation objects while painting the snapshot bitmap; only the render thread samples and draws them for the live window.

### Overlay Layer

The overlay layer is transparent and optional. It contains layout-edit affordances, selected-tree highlights, drag outlines, dragged-child static replay that must appear above underlying animations, metric-list dragged-row replay, and the move overlay.

The renderer calls `DashboardOverlayState::ShouldDrawOverlayLayer()` before entering the overlay pass. Normal dashboard operation keeps that predicate false, so the overlay pass and overlay animation flush are skipped completely.

The overlay pass has three ordered sublayers:

- Background edit affordances for fixed dashboard content.
- Overlay-owned widget content, including metric-list dragged-row replay and container-child dragged-content replay.
- Foreground edit affordances attached to the active dragged row or dragged child.

Widgets draw overlay-owned content through `Widget::DrawOverlay()` and submit `WidgetAnimationLayer::Overlay` animations. Container-child drag replay also draws widgets in the overlay layer under the active drag translation. Edit affordances carry layout-edit owner tags and an overlay sublayer tag when they are registered; active drag state promotes affordances owned by the dragged row or child to the foreground sublayer, while fixed-content affordances stay in the background sublayer. Snapshot and overlay animations resolve through the same render-thread `DashboardAnimationTimeline`, so moving a widget or row from the snapshot layer to the overlay layer during drag does not reset ongoing data interpolation.

### Animation Draw Lists

Animation draw lists are not stored as bitmaps. Each list contains immutable widget animation draw objects plus the render-space translation that was active when the widget submitted the animation. The keyed data timeline is owned by the render thread, which redraws the current widget animation commands for the active layer on each animation frame.

Widget animation objects contain renderer-safe geometry. Animation draw commands carry the widget-packaged target state plus layer placement data such as drag translation; this keeps geometry changes separate from metric target changes. They do not retain metric-source references, config references, string views, or main-thread-owned containers. Concrete animation data types stay private to the widget package; the render thread stores and samples them through the opaque `WidgetAnimationState` and `WidgetAnimationTransition` interfaces.

The main thread repaints the opaque snapshot bitmap when telemetry text changes, layout or scale changes, theme/style changes, render mode changes, or layout-edit drag state changes. It repaints the optional transparent overlay bitmap on layout-edit hover changes, drag changes, move-mode changes, and editor selection changes. Normal dashboard operation keeps no overlay bitmap alive.

## Threading Model

### Telemetry Worker

The telemetry worker keeps its current responsibility: collect telemetry on a 500 ms cadence, skip missed intervals after stalls or sleep, and publish copied updates through the existing sink contract.

### Main Thread

The main thread owns Win32 input, menu flow, layout-edit interaction, config mutation, layout resolution, deterministic offscreen rendering, and layer construction.

For the live dashboard, the main thread:

- Accepts telemetry updates from the existing pending-update handoff.
- Resolves the latest `MetricSource`.
- Builds immutable snapshot and overlay widget animation scenes from the resolved layout and metric data.
- Paints the opaque snapshot bitmap and optional transparent overlay bitmap, skipping the overlay work when no overlay content is visible.
- Publishes the most recent layer update to the render thread through an overwrite-only mailbox.

The main thread does not queue stale layer updates. If a new drag frame or telemetry snapshot is ready before the render thread consumes the previous update, the pending unpublished frame is replaced by the newest complete frame.

### Render Thread

The render thread owns the HWND presenter, the live render-target device state, animation timelines, target-local bitmap uploads, and frame presentation.

The render thread:

- Consumes only the latest mailbox update.
- Keeps previous animation data by key.
- Computes interpolated animation values for the current frame time.
- Composes snapshot, snapshot animations, optional overlay, and overlay animations in that order.
- Presents the frame through the package-private presenter while animations are active. The current backend uses the Direct2D HWND target; the planned DXGI flip-model swap chain remains inside `renderer`.
- Sleeps when no animation and no new layer update is pending.

The render-thread HWND/device API remains package-private inside `dashboard_renderer`; shell code initializes the dashboard renderer with an HWND but does not receive direct render-thread controls.

## Animation Scene Contracts

### Data Keys

Animation data is keyed independently from draw geometry so multiple primitives can share the same interpolated data.

`AnimationDataKey` contains:

- `std::string subject`.
- Optional `std::string lane`.

The concrete data category is not encoded into the key and is not exposed to the dashboard renderer. The key itself represents the stable logical data source only.

Stable subjects use metric refs where possible:

- Metric-list rows and gauges use the metric ref, such as `cpu.load` or `gpu.vram`.
- Throughput charts use the throughput metric ref, such as `network.upload`.
- Drive usage uses the stable drive label or letter plus `used`.
- Drive activity uses the stable drive label or letter plus `read` or `write`.

The key follows the logical data source rather than the visual slot. Reordering metric rows or moving widgets preserves interpolation continuity when the same metric remains visible.

### Widget Animation Objects

Widgets resolve metric data while they still have access to `MetricSource`. Each widget draws its snapshot content, packages the target animation data into a widget-private `WidgetAnimationState`, and submits a `WidgetAnimation` through `WidgetHost::AddWidgetAnimation()`. The dashboard renderer stores that opaque target state in the presentation command, so the render thread samples it between telemetry updates without asking widget code to resolve target data again.

The public animation interface exposes:

- `AnimationDataKey` for stable logical identity.
- `WidgetAnimationLayer`, which identifies whether the animation belongs to the snapshot layer or overlay layer.
- `WidgetAnimation`, which can return a target state and draw a sampled state on a regular `Renderer`.
- A conservative dirty bound for the animation command. This is a generic render-space invalidation rectangle, not widget-private geometry.
- `WidgetAnimationState`, which can clone itself, create first-seen and retarget starts, compare compatible targets, and create transitions.
- `WidgetAnimationTransition`, which samples an opaque state at a normalized progress value.

The dashboard renderer never resolves metrics for animations and never switches on scalar, throughput, gauge, or activity payload types. It keeps old and new opaque states by `AnimationDataKey` and asks the target state to build the transition.

### Widget-Private Scalar Samples

`ScalarFillSample` is a widget-private data object for pill bars, gauge values, drive usage, and drive activity.

Fields:

- `std::optional<double> valueRatio` - normalized value in `[0, 1]`; `std::nullopt` means unavailable and draws no value fill.
- `std::optional<double> peakRatio` - normalized recent-peak or recent-max indicator in `[0, 1]`; `std::nullopt` means no peak indicator.

Widget-private state construction clamps finite values before they enter the dashboard timeline. It stores `std::nullopt` instead of NaN or infinity and clamps again before drawing so stale or malformed data cannot escape into geometry.

### Widget-Private Throughput Samples

`ThroughputChartSample` is a widget-private data object for throughput chart animation.

Fields:

- `std::vector<double> samples` - smoothed retained-history samples in display order.
- `double maxGraph` - shared vertical graph maximum for the chart group.
- `double timeMarkerOffsetSamples` - horizontal time-marker phase in sample units.
- `double plotShiftSamples` - per-frame fractional plot shift in sample units; positive values draw the sample
  sequence further left while target tail samples enter from the right.
- `double guideStepMbps` - guide spacing selected from the target max.

The sample vector uses the same smoothed adjacent-pair history that `MetricSource::ResolveThroughput()` exposes today. Widget-private state construction treats non-finite sample values as `0`.

Concrete animation geometry is a widget implementation detail. Pill bars, gauges, throughput charts, and drive activity indicators each own small private `WidgetAnimation` implementations that know how to draw their sampled private state onto `Renderer`.

## Interpolation

### Scalar Interpolation

Scalar fill values and peak values use linear interpolation:

```text
value = start + (target - start) * progress
```

`progress` is clamped to `[0, 1]` across the 500 ms animation duration.

First-seen and unavailable values behave as follows:

- Missing previous value to available: the render thread synthesizes a zero start value and animates from zero to the target.
- Available to unavailable: the target becomes zero; the value fill animates down to zero, then disappears.
- Unavailable to available: the render thread synthesizes a zero start value and animates from zero to the target.
- Missing peak to present peak: the peak marker appears at the target position.
- Present peak to missing peak: the peak marker animates to zero with the value and then disappears.

### Gauge Interpolation

Gauge animation uses `ScalarFillSample`. The gauge value interpolates as a continuous normalized value, then renders as whole filled segments for the current interpolated value. Segment gaps remain empty, and the draw path does not render partial segment sweeps.

The peak indicator uses the interpolated `peakRatio` and snaps to the whole segment representation chosen for gauge peak drawing.

### Activity Interpolation

Drive read/write activity animation uses `ScalarFillSample`. The value interpolates continuously, then renders as whole filled stacked segments for the current interpolated value. Segment gaps remain empty, and partial segment fill is not drawn.

### Throughput Interpolation

Throughput chart animation interpolates the visible chart shape rather than only the newest point:

- `maxGraph` interpolates linearly so vertical rescaling is smooth.
- `timeMarkerOffsetSamples` interpolates forward in sample units and the draw path normalizes it by marker
  interval, so time markers drift smoothly through interval wraps.
- The leader y-position interpolates from the previous displayed latest value to the new displayed latest value.
- Plot samples carry a fractional `plotShiftSamples` phase, so the draw path moves existing sample points left
  while target tail samples enter from the right instead of morphing fixed columns into each other.

When the previous and target sample vectors have different lengths, the render thread aligns both vectors by the newest sample. Missing older samples use `0`.

The max-value text label belongs to the snapshot layer and updates only on telemetry snapshots. Guide lines whose position depends on `maxGraph` belong to the animation layer so graph rescaling does not jump while the label changes.

## Layout Edit And Drag Interaction

Layout-edit interaction stays orchestrated by the main thread. The render thread never performs hit testing or config mutation.

Dynamic edit artifacts are collected from the snapshot build using the target telemetry snapshot and target layout geometry. Hit testing does not follow per-frame interpolated animation geometry.

During a metric-list row reorder drag, the main thread publishes animation geometry for the row's current drag position. The vacated row slot does not publish a duplicate animation primitive for that row.

During a container-child reorder drag, the dragged child keeps animating while it tracks the pointer above underlying dashboard content. The main thread draws the dragged child's static content into the overlay layer and tags that child's animation primitives as `WidgetAnimationLayer::Overlay`. Underlying widgets keep ordinary `WidgetAnimationLayer::Snapshot` primitives. The main thread identifies the dragged child by layout-edit owner tags rather than by rectangle containment and does not compute drag-overlap visibility subtraction for animation primitives.

Move mode uses the overlay layer for monitor name, scale, and relative-coordinate text. The dashboard content underneath continues to animate unless move-mode throttling is explicitly enabled later.

## Renderer And Bitmap Ownership

The implementation keeps widget semantics outside `renderer` and backend details inside `renderer`.

Renderer-facing additions are generic:

- Opaque and transparent layer bitmap allocation.
- Drawing into a caller-provided layer bitmap.
- Uploading or binding a layer bitmap for composition.
- Drawing a bitmap at a render-space origin.
- Presenting a composed live frame with optional dirty rectangles.
- Presenting the live frame through a backend-owned swap chain or HWND target.

`renderer` does not know about metrics, widgets, animation keys, gauges, charts, or layout-edit drag rules.

Each thread owns its renderer caches:

- The main thread owns the offscreen layer painter and its palette/text/icon caches.
- The dashboard renderer owns the cross-thread layer bitmap pool. The main thread acquires writable layer bitmaps from it, and the render thread returns superseded frame-owned bitmaps after presentation handoff.
- The render thread owns the live presenter and its palette/text/icon/target-local bitmap caches.
- Palette and renderer style updates are copied by value into each thread. No thread mutates a palette that another thread can read.

If Direct2D resources are shared across threads, the Direct2D factory is created in multithreaded mode and access follows the Direct2D multithread locking contract. If layer bitmaps are CPU/WIC-backed and uploaded per update, each renderer instance may keep thread-local Direct2D factories instead.

## Window Size And DPI Synchronization

Window size, render scale, and DPI changes use a monotonically increasing `surfaceVersion`.

Each layer update carries:

- `surfaceVersion`.
- Pixel width and height.
- Render scale.
- Snapshot version.
- Overlay version.
- Animation geometry version.
- Metric version.
- Style version.

The render thread treats an update whose surface version differs from the current live target as a target-recreation boundary. When the version changes, it:

- Finishes the current frame if one is in progress.
- Releases target-local layer bitmaps for the old size.
- Recreates or resizes the live presentation target.
- Drops animation geometry from the old surface version.
- Keeps animation data only when the new update carries compatible data keys.

The render-thread and single-thread benchmark implementations follow the same data rule inside `DashboardAnimationTimeline`: config,
layout, row-order, scale, and render-mode changes keep keyed opaque widget animation tracks alive. A track is removed
only when its data key is not touched by the next live normal frame, so compatible metrics continue from their current
interpolated value after layout edits instead of restarting from zero.

The main thread always publishes a complete snapshot layer after a size, DPI, scale, or layout change. Partial updates are valid only within one surface version.

## Dirty Rectangles

Telemetry snapshot updates and layout/style changes present the full window because they update the snapshot layer or animation geometry. When animation is enabled, those full redraws use the same retained presenter target as dirty animation frames so the HWND target is not recreated between a metrics update and the following animation ticks.

Animation geometry changes only when the snapshot version or overlay version changes. If neither version changed, animation geometry is unchanged and animation-only frames prepare each current animation primitive once, collecting its conservative dirty bound and sampled draw state together. The retained dirty presenter restores those non-coalesced regions from the snapshot bitmap in one batched region-copy step, restores the same regions from the overlay bitmap when an overlay exists, then draws each prepared animation once. Dirty bounds are conservative enough to contain the animation's draw output, so the render thread does not need per-rectangle animation clipping.

When the snapshot version or overlay version changes, the render thread treats the whole live surface as dirty. Config edits, layout edits, and active drags redraw at least one of those layers on the main thread, so the render thread does not try to infer smaller dirty regions from old and new animation geometry. During active drags, the overlay bitmap version changes on each drag frame, and the render thread cannot inspect the overlay's internal delta.

## Module Ownership

The first implementation should avoid a new top-level source package unless the source dependency rules are updated in the same change. The target ownership is:

- `src/widget/animation_types.h`
  - Owns public animation identity through stable data keys.
  - Depends only on standard library types and lower-level utility helpers when needed.
- `src/widget/animation.h`
  - Owns the public opaque animation interfaces: `WidgetAnimation`, `WidgetAnimationState`, and `WidgetAnimationTransition`.
  - Exposes drawing only as `Renderer&` plus an opaque sampled state.
- `src/widget/impl/animation_primitives.*`
  - Owns widget-private scalar and throughput sample state, transition, interpolation, and sanitization implementations.
  - Is not included from outside the `widget` package.
- `src/widget/impl/pill_bar.*`
  - Keeps pill-bar geometry helpers.
  - Adds helpers that produce `PillBar` animation primitives and draw pill-bar snapshot tracks.
- `src/widget/impl/gauge.*`
  - Keeps gauge layout-state resolution.
  - Adds helpers that produce `Gauge` animation primitives and draw gauge snapshot tracks/text.
- `src/widget/impl/drive_usage_list.*`
  - Adds `PillBar` primitives for usage bars and `StackedActivity` primitives for read/write indicators.
- `src/widget/impl/metric_list.*`
  - Adds `PillBar` primitives for each visible metric row and keeps row text in the snapshot layer.
- `src/widget/impl/throughput.*`
  - Adds `ThroughputChart` primitives and keeps chart label text in the snapshot layer.
- `src/widget/widget_host.h`
  - Exposes widget animation submission through `AddWidgetAnimation()`.
  - Does not expose widget-private sample types, primitive geometry, or transition details.
- `src/dashboard_renderer/dashboard_renderer.*`
  - Owns live scene building, deterministic draw fallback, layer bitmap build orchestration, and render-thread handoff.
  - Keeps the immediate target-value draw path for deterministic offscreen rendering.
- `src/dashboard_renderer/impl/animation_timeline.*`
  - Owns render-thread interpolation state, opaque data-key maps, interruption handling, and state sampling.
  - Asks widget-private state objects to create zero-initialized starts, retarget starts, and transitions.
- `src/dashboard_renderer/impl/render_thread.*`
  - Owns the live render thread, single-thread benchmark presenter, mailbox, surface version, presentation loop, renderer/presenter instance, and render-thread animation timeline.
- `src/renderer/*`
  - Owns generic bitmap, composition, and presentation primitives only.
  - Keeps Direct2D, DirectWrite, WIC, Direct3D, and DXGI implementation details inside `src/renderer/impl/`.
- `src/telemetry/telemetry.h`
  - Exposes the shared 500 ms telemetry refresh cadence constant consumed by both `TelemetryRuntime` and dashboard animation.
- `src/dashboard/dashboard_app.*`
  - Keeps shell input, invalidation, telemetry update draining, and layout-edit interaction.
  - Calls dashboard-renderer APIs instead of owning render-thread details.
- `tests/*`
  - Adds focused tests for scalar interpolation, interrupted animations, throughput vector alignment, surface-version target recreation, and animation composition-plane tagging.
- `tests/benchmarks.cpp`
  - Keeps deterministic paint benchmarks single-threaded while forcing the same layer-bitmap build and final composition steps as the live pipeline.
  - Owns the `animation` benchmark, which builds one fake-metric, no-overlay live presentation frame and repeatedly presents the stored frame through the render-thread animation timeline and composition path while keeping each measured frame inside the active transition window.

## Validation

Implementation validation should use staged checks:

- Unit-test interpolation and composition-plane tagging without creating a window.
- Keep existing screenshot, active-region, layout-guide-sheet, and widget tests deterministic by rendering target values directly.
- Run `build.cmd` and `test.cmd` after implementation changes.
- Run `build.cmd /benchmarks` and compare `animation`, `edit-layout`, `mouse-hover`, `layout-switch`, `theme-change`, and `update-telemetry` against [docs/profile_benchmark.md](profile_benchmark.md) baselines when live draw-path behavior changes.
- Add focused diagnostics screenshot validation for blank mode, layout-edit hover, metric-list row drag, container-child drag, and throughput chart rendering.

## Resolved Design Choices

- Drive read/write stacked activity indicators animate in the first implementation.
- Gauge and drive-activity visuals render whole filled segments during animation.
- The live presenter is isolated behind `DashboardRenderThread`; the current backend uses the existing Direct2D HWND target, and DXGI flip-model presentation remains the next renderer-backend replacement.
- Dragged widgets and container children keep animating while dragged; dragged-child animation primitives render above the overlay layer.
- First-seen animation keys animate from zero, including application startup.
- Unavailable targets animate down to zero before disappearing.
- Animation duration is the shared 500 ms telemetry cadence constant.
- Diagnostics screenshots render target values, including UI-attached diagnostics screenshots.
- Throughput max text updates on telemetry snapshots with the rest of the text; only drawn chart geometry animates.
