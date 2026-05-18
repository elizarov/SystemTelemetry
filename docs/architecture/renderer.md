# Renderer Package

`src/renderer/` owns render-space contract types, renderer style DTOs, the D2D-free `Renderer` interface, renderer style resources, render-target lifecycle, and the Direct2D/DirectWrite/WIC implementation under `src/renderer/impl/`.

## Responsibilities

- Provide primitive drawing for text, rectangles, rounded rectangles, ellipses, lines, arcs, polylines, filled paths, clips, and translations.
- Own live HWND rendering through a renderer-private DXGI flip-model swap chain, retained full-window rendering for animation-capable frames, retained dirty-window rendering, text measurement, shared offscreen bitmap rendering for screenshot export and validation priming, icon loading, and compressed PNG encoding for generated renderer-owned pixel buffers.
- Present live DXGI flip-chain frames with vsync; animation frame cadence comes from the presentation backend rather than a render-thread timer. Benchmark immediate-present paths keep the DXGI/device-resource backend but present without vsync so direct benchmark timings do not include monitor cadence.
- Restore dirty regions into the retained window back buffer before presenting animation frames; dirty rectangles are a renderer-internal redraw contract, not DXGI presentation metadata. Flip-chain targets prime every physical back buffer with a full composition after retained full redraws before returning to dirty-only animation frames.
- Provide generic opaque bitmap rendering and bitmap composition primitives through `RenderBitmap`, `DrawToBitmap()`, `DrawBitmap()`, `DrawBitmapRegion()`, and `DrawBitmapRegions()`. `DrawToBitmap()` is the deterministic WIC-backed path used for offscreen export and validation.
- Provide the live layer bitmap contract separately through `DrawToLiveLayerBitmap()`. Live layer bitmaps are tagged as `RenderBitmapStorage::LiveLayer`, stay opaque behind `RenderBitmapResource`, and are the only resources eligible for the dashboard live-layer bitmap pool.
- Keep resolved RGBA palettes private, map render color ids internally, and cache Direct2D solid brushes by palette id instead of arbitrary RGB values.
- Decode the embedded 8-bit grayscale panel-icon mask atlas through WIC, crop fixed icon slots, and draw them through a target-local Direct2D alpha mask.
- Use non-owning `FunctionRef` views for synchronous draw and measurement callbacks; callback storage remains owned by the caller.

## Boundaries

- `renderer` may depend on `renderer`, `config`, `util`, and the synthetic `d2d` package.
- The public renderer interface is D2D-free. Direct2D, Direct3D, DirectWrite, DXGI, WIC, and WRL includes stay inside renderer implementation modules.
- Renderer does not own dashboard drawing modes, overlay policy, trace emission, diagnostics orchestration, or widget semantics.
