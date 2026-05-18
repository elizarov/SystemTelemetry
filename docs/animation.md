# Dashboard Animation

This document owns live-dashboard animation behavior and the architecture that supports it.
See also: [specifications.md](specifications.md) for user-visible dashboard behavior, [layout_edit.md](layout_edit.md) for layout-edit interaction, [diagnostics.md](diagnostics.md) for trace and export behavior, [profile_benchmark.md](profile_benchmark.md) for benchmark workflow and baselines, and [architecture.md](architecture.md) for package boundaries.

## Purpose

The live dashboard animates metric visuals between telemetry snapshots so values feel continuous across the shared 250 ms telemetry cadence while idle CPU usage stays low.

Animation applies to data-driven visuals:

- Metric-list pill-bar fill and recent-peak marker.
- Drive-usage pill-bar fill and read/write stacked activity fill.
- Gauge value fill and recent-peak indicator.
- Throughput chart plot, leader position, time-marker phase, and vertical scale.

Animation does not apply to text, card chrome, widget labels, layout geometry, or edit affordance strokes. Text updates when a telemetry snapshot is accepted.

## Behavior

On each accepted telemetry snapshot, the main thread publishes snapshot text and widget-packaged animation target states. The first frame for that snapshot shows the snapshot text while animated visuals continue from their previous sampled values. Animated visuals interpolate to their targets over the 250 ms telemetry cadence.

If another telemetry snapshot arrives during an active interpolation, the render thread samples the current visual state and uses that sampled state as the start for the next transition. Motion remains continuous.

When an animation key appears without stored render-thread state, the timeline creates a zero-initialized start state. This applies at startup and when a metric first appears after layout, binding, row-order, or drive-selection changes.

When a metric target is unavailable, permission-gated, or otherwise not drawable, text updates immediately and the visual animates from the current value to zero. The visual disappears after the zero-value transition completes.

Layout edits, row reordering, scale changes, and surface changes do not reset compatible animation data. Animation state is stored by logical data key and remains independent from render-space geometry. Geometry changes move where the sampled state is drawn.

Animation runs while at least one transition is active or while the main thread publishes layer updates. After transitions reach their targets, the render thread presents the settled frame and waits.

Blank render mode, screenshot exports, layout guide sheet exports, app icon exports, unit tests, and other deterministic offscreen renders draw target snapshot values directly. UI-attached diagnostics screenshots also render target values instead of the currently interpolated live frame.

## Frame Pipeline

`DashboardRenderer` builds live presentation frames as:

- An opaque snapshot bitmap.
- A snapshot animation command list.
- An optional transparent overlay bitmap.
- An overlay animation command list.
- A copied renderer style payload and presentation version record.

The main thread resolves layout, metrics, text, edit artifacts, and widget animation targets. It draws snapshot and overlay bitmaps into renderer-owned bitmap resources and records widget animation commands during those draw passes.

The render thread owns HWND presentation, the live renderer instance, the animation timeline, retained frame state, and frame presentation. It composes snapshot bitmap, snapshot animations, overlay bitmap, and overlay animations in that order.

The dashboard renderer publishes frame updates through an overwrite-style mailbox. Pending frame updates are coalesced before the render thread consumes them, so a layer update cannot replace an unpublished complete layer that it depends on. The render thread merges consumed frame updates into its retained active frame.

During live layer bitmap construction, the main thread can suspend animation-only presentation. The render thread waits for the layer update instead of drawing the retained frame through the shared Direct2D device; timeline sampling still uses render-thread clock time.

## Layer Model

### Snapshot Layer

The snapshot layer is opaque. It contains:

- Dashboard background, card chrome, headers, static labels, and all text.
- Animated-widget static backgrounds, including pill-bar tracks, gauge track segments, chart background, chart axes, and non-animated chart labels.
- Static content that belongs below animated fills for the current layout-edit state.

Widgets draw snapshot content through `Widget::Draw()` and submit animation primitives through `WidgetHost::AddWidgetAnimation()`. The dashboard renderer records those commands in the snapshot animation list while painting the snapshot bitmap.

### Overlay Layer

The overlay layer is transparent and optional. It contains layout-edit affordances, selected-tree highlights, drag outlines, dragged-child static replay, metric-list dragged-row replay, and the move overlay.

`DashboardOverlayState::ShouldDrawOverlayLayer()` gates overlay work. When it returns false, the overlay bitmap pass and overlay animation list are skipped.

The overlay pass uses three ordered sublayers:

- Background edit affordances for fixed dashboard content.
- Overlay-owned widget content, including metric-list dragged-row replay and container-child dragged-content replay.
- Foreground edit affordances attached to the active dragged row or dragged child.

Edit artifacts carry layout-edit owner tags and overlay sublayer information when registered. Active drag state promotes affordances owned by the dragged row or dragged child to the foreground sublayer; fixed-content affordances stay in the background sublayer. Layer assignment does not depend on rectangle containment.

Widgets draw overlay-owned content through `Widget::DrawOverlay()` and submit overlay animations through `WidgetHost::AddWidgetAnimation()`. Snapshot and overlay animation lists resolve through the same render-thread timeline, so moving a widget or row between layers during drag preserves interpolation.

## Widget Animation Contract

Widgets resolve metric data while they have access to `MetricSource`. A widget draws static snapshot or overlay content, packages animation target data into a widget-private `WidgetAnimationState`, and submits it with a geometry-only `WidgetAnimation`.

The public widget animation interface exposes:

- `AnimationDataKey` for stable logical identity.
- `WidgetAnimationLayer` for the dashboard renderer's active collection layer.
- `WidgetAnimation`, which provides the data key, conservative dirty bounds, and sampled-state drawing on a regular `Renderer`.
- `WidgetAnimationState`, which clones itself, creates first-seen and retarget starts, compares compatible targets, and creates transitions.
- `WidgetAnimationTransition`, which samples an opaque state at normalized progress.

Concrete target payloads, interpolation types, and animation geometry stay private to the widget package. The dashboard renderer and render thread do not switch on scalar, throughput, gauge, or activity payload categories.

`WidgetAnimation` objects are constructed on the main thread as immutable presentation commands and are invoked by the render thread for dirty bounds and drawing. Commands do not retain metric-source references, config references, string views, or main-thread-owned containers.

## Data Keys

Animation data is keyed independently from draw geometry so logical data can keep interpolating while its screen position changes.

`AnimationDataKey` contains:

- `subject`.
- Optional `lane`.

The key represents the stable logical data source. Metric-list rows and gauges use metric refs, throughput charts use throughput metric refs, drive usage uses a stable drive label or letter plus `used`, and drive activity uses a stable drive label or letter plus `read` or `write`.

Keys follow data, not visual slots. Reordering rows or moving widgets preserves interpolation when the same metric remains visible.

## Widget-Private Samples

`ScalarFillSample` is the widget-private state used by pill bars, gauges, drive usage, and drive activity. It stores an optional normalized value ratio and optional normalized peak ratio. Missing values draw no fill or marker. State construction clamps finite values to `[0, 1]` and stores `std::nullopt` for NaN or infinity.

`ThroughputChartSample` is the widget-private state used by throughput charts. It stores compact retained-history body samples in display order, the live leader value, shared vertical graph maximum, time-marker offset in one-second sample units, fractional plot shift in one-second sample units, and guide spacing. Non-finite sample values become `0`. Throughput retained history keeps 30 ready-to-draw one-second averages plus the last four raw 250 ms samples for the live leader, so the chart still covers 30 seconds while transferring and drawing fewer points. Scalar retained histories separately keep 120 raw 250 ms samples for peak ghosts on bars and gauges.

Widget-private animation primitives own their render geometry. They draw sampled private state onto `Renderer` and report conservative dirty bounds for retained composition.

## Interpolation

Scalar fill values and peak values use clamped linear interpolation:

```text
value = start + (target - start) * progress
```

`progress` runs from `0` to `1` across the 250 ms animation duration.

First-seen and unavailable scalar values use these rules:

- Missing previous value to available target: animate from zero to the target.
- Available value to unavailable target: animate to zero, then disappear.
- Unavailable value to available target: animate from zero to the target.
- Missing peak to present peak: show the peak at the target position.
- Present peak to missing peak: animate the peak toward zero with the value, then disappear.

Gauge animation uses `ScalarFillSample`. The value interpolates continuously, then renders as whole filled gauge segments. Segment gaps remain empty.

Drive read/write activity uses `ScalarFillSample`. The value interpolates continuously, then renders as whole filled stacked segments. Segment gaps remain empty.

Throughput chart animation interpolates visible chart shape:

- `maxGraph` interpolates linearly so vertical rescaling is smooth.
- `timeMarkerOffsetSamples` interpolates forward in sample units and normalizes by marker interval.
- The leader y-position interpolates from the previous displayed latest value to the target displayed latest value.
- `plotShiftSamples` moves existing sample points left while target tail samples enter from the right.

When previous and target throughput sample vectors have different lengths, the timeline aligns both vectors by newest sample. Missing older samples use `0`.

Throughput max-value text belongs to the snapshot layer and updates on telemetry snapshots. Guide lines whose position depends on `maxGraph` belong to the animation layer.

## Timeline

`DashboardAnimationTimeline` lives on the render thread. It stores keyed opaque widget states, creates transitions, samples active transitions, and prunes tracks.

Metric-version changes retarget animation data. Layout, row-order, scale, surface, and render-mode changes can replace geometry and layer bitmaps without replacing compatible keyed targets. A track is pruned on a live frame whose `metricVersion` differs from the stored version and no longer touches that data key.

All snapshot and overlay animation commands in a frame share one timeline. A data key can move between layers without creating a second data track.

Timeline resets are explicit lifecycle events, such as disabling live animation or shutting down presentation. Trace output records animation timeline resets and track pruning through renderer-prefixed trace lines.

## Threading

### Telemetry Worker

`TelemetryRuntime` collects telemetry on the shared 250 ms cadence, skips missed intervals after stalls or sleep, and publishes copied updates through its sink contract.

### Main Thread

The main thread owns Win32 input, menu flow, layout-edit interaction, config mutation, layout resolution, deterministic offscreen rendering, and layer bitmap construction.

For live rendering, the main thread:

- Accepts telemetry updates from the pending-update handoff.
- Resolves the latest `MetricSource`.
- Builds immutable widget animation scenes from the resolved layout and metrics.
- Paints the snapshot bitmap and optional overlay bitmap.
- Publishes the latest presentation frame update to the render thread.

The main thread does not draw live animation frames and does not own the animation timeline.

### Render Thread

`DashboardRenderThread` owns the HWND presentation renderer, animation timeline, retained active frame, presentation versions, and frame loop.

The render thread:

- Consumes coalesced mailbox updates.
- Keeps previous animation data by key.
- Samples interpolated animation values for the current frame time.
- Restores dirty retained regions from layer bitmaps when possible.
- Draws sampled animation commands.
- Presents through the renderer's live presentation backend.
- Waits when no animation or frame update is pending.

The render-thread presentation path is package-private to `dashboard_renderer`. Public `DashboardRenderer` APIs expose live drawing, deterministic rendering, snapshot export, and layout-edit queries without exposing render-thread frame structures.

## Renderer And Bitmap Ownership

The renderer package owns generic bitmap and presentation primitives. It does not know about metrics, widgets, animation keys, gauges, charts, or layout-edit drag rules.

The main thread draws live snapshot and overlay layers into `RenderBitmapStorage::LiveLayer` resources through `Renderer::DrawToLiveLayerBitmap()`. Live layer bitmaps are opaque `RenderBitmapResource` objects backed by shared Direct3D/Direct2D resources. They move between the main thread and render thread without CPU readback.

`DashboardRenderer` owns the cross-thread live-layer bitmap pool. The main thread acquires writable live-layer bitmaps by surface size, and the render thread returns superseded active or pending frame layers after handoff. Returned bitmaps whose size no longer matches the active surface are rejected.

Deterministic exports and validation use generic WIC-backed `Renderer::DrawToBitmap()` resources. Generic bitmaps do not enter the live-layer pool.

Each thread owns its renderer instance and renderer caches. Renderer style and palette payloads are copied by value into presentation frames.

Live presentation uses the renderer-private DXGI flip-model backend with vsync. Animation frame cadence comes from presentation pacing, not a fixed render-thread timer. Benchmark immediate-present paths use the same device-resource backend without waiting for vsync.

## Surface Changes

Window size, render scale, and DPI changes increment `surfaceVersion`. Presentation frames and presented-frame state share `DashboardPresentationVersions` so version semantics stay identical across the handoff.

Each presentation frame carries:

- `surfaceVersion`, which guards live presentation target recreation.
- `snapshotVersion`, which guards opaque snapshot bitmap replacement.
- `overlayVersion`, which guards transparent overlay bitmap replacement.
- `animationGeometryVersion`, which guards dirty-animation geometry reuse.
- `metricVersion`, which guards animation target retargeting and stale-key pruning.
- Pixel width, pixel height, animation flag, and renderer style payload.

When `surfaceVersion` changes, the render thread recreates the live presentation target and drops geometry from the superseded surface. Compatible metric animation data remains keyed in the timeline.

Before the HWND size changes, the UI thread asks the render thread to discard its current target and frame. The size change uses no-redraw, no-copy-bits window positioning, and the live swap chain uses non-stretch scaling. The UI thread then publishes and waits for one frame for the resized surface so the window manager does not stretch the previous swap-chain image into the resized shape.

Surface changes also retarget the live-layer bitmap pool to the active size.

## Dirty Regions

Telemetry snapshot updates and layout or style changes present the full live surface because they update layer bitmaps or animation geometry.

Animation-only frames use retained dirty-window composition. The render thread prepares each current animation primitive once, collects its conservative dirty bound and sampled state, restores those non-coalesced regions from the snapshot bitmap, restores the same regions from the overlay bitmap when an overlay exists, and draws each prepared animation once.

Dirty bounds are also used as animation clip bounds. Animation output cannot leak outside the restored retained-buffer region or survive on another flip-chain buffer.

Dirty rectangles are renderer-internal redraw regions. Renderer backends may use them for retained target restoration and redrawing; they are not required to forward them as DXGI dirty-present metadata.

When snapshot, overlay, or animation geometry versions change, the render thread treats the whole surface as dirty. Active drags update at least one layer bitmap, so the render thread does not infer smaller geometry deltas from superseded and active drag state.

## Layout-Edit Interaction

Layout-edit interaction stays on the main thread. The render thread never performs hit testing or config mutation.

Dynamic edit artifacts are collected from target snapshot geometry. Hit testing does not follow per-frame interpolated animation geometry.

During metric-list row reordering, the dragged row's static content and animations are recorded in the overlay layer at the drag translation. The vacated row slot does not publish a duplicate animation primitive for that row.

During container-child reordering, the dragged child is replayed in the overlay layer under the drag translation, including its animation commands. Underlying fixed widgets keep their snapshot animation commands.

Move mode uses the overlay layer for monitor name, scale, and relative-coordinate text. Dashboard content underneath continues to animate.

## Benchmarks And Profiling

`CaseDashBenchmarks` uses package-private benchmark access in `dashboard_renderer/impl/dashboard_renderer_benchmark.*` to build and publish presentation frames without exposing render-thread frame structures through the public renderer header.

The `animation` benchmark builds one fake-metric, no-overlay live presentation frame, hands it to a benchmark render worker, and repeatedly requests stored-frame presentation through the render-thread animation timeline and composition path. The measured loop includes main-to-render-thread request handoff and per-frame `WidgetAnimationTransition::Sample()` work. It uses immediate non-vsynced presentation so monitor cadence is not part of the result.

The `snapshot-handoff` benchmark forces a fake-metric snapshot revision each iteration, rebuilds the no-overlay snapshot layer into pooled live-layer bitmaps, publishes through the live asynchronous render-thread handoff, and reports frame-build cost separately from publish cost. It keeps a visible render-thread HWND presenting on vsync and excludes cadence waits from timing totals.

The paint benchmarks remain single-threaded where they measure deterministic paint paths, but they include snapshot and overlay bitmap construction plus final bitmap-and-animation composition steps so they reflect the live renderer pipeline.

Runtime `/trace-prefixes:profile` timing lines use operation names that match benchmark phases. The `animation_frame` timing measures animation sampling and composition work and excludes the live DXGI vsync wait.

## Package Ownership

Animation code follows the maintained package boundaries documented under [architecture/](architecture/):

- `widget` owns animation identity, opaque animation interfaces, widget-private sample states, transitions, and concrete animation primitives.
- `dashboard_renderer` owns layer construction, widget-host animation collection, live-layer bitmap pooling, frame handoff, and the render-thread animation timeline.
- `renderer` owns generic D2D-free bitmap, retained composition, dirty redraw, and live presentation APIs plus Direct2D/DirectWrite/WIC/DXGI implementation details.
- `telemetry` owns the shared 250 ms refresh cadence and the snapshot data consumed by widget metric resolution.
- `dashboard` owns shell invalidation, telemetry update draining, window-size synchronization, and layout-edit interaction flow.

`dashboard_renderer/impl/render_thread.*` and `dashboard_renderer/impl/dashboard_renderer_benchmark.*` are package-private implementation modules. Production include sites use `DashboardRenderer` instead of render-thread presentation structures.

## Validation

Animation changes use the standard project validation entrypoints from [docs/build.md](build.md).

Changes that affect live animation, layer construction, dirty composition, or frame handoff also build benchmarks with `build.cmd /benchmarks` and compare relevant benchmark output against [profile_benchmark.md](profile_benchmark.md).
