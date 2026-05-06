# Renderer Package

`src/renderer/` owns render-space contract types, renderer style DTOs, the D2D-free `Renderer` interface, renderer style resources, render-target lifecycle, and the Direct2D/DirectWrite/WIC implementation under `src/renderer/impl/`.

## Responsibilities

- Provide primitive drawing for text, rectangles, rounded rectangles, ellipses, lines, arcs, polylines, filled paths, clips, and translations.
- Own live HWND rendering, text measurement, shared offscreen bitmap rendering for screenshot export and validation priming, icon loading, and compressed PNG encoding for generated renderer-owned pixel buffers.
- Keep resolved RGBA palettes private, map render color ids internally, and cache Direct2D solid brushes by palette id instead of arbitrary RGB values.
- Decode the embedded 8-bit grayscale panel-icon mask atlas through WIC, crop fixed icon slots, and draw them through a target-local Direct2D alpha mask.
- Use non-owning `FunctionRef` views for synchronous draw and measurement callbacks; callback storage remains owned by the caller.

## Boundaries

- `renderer` may depend on `renderer`, `config`, `util`, and the synthetic `d2d` package.
- The public renderer interface is D2D-free. Direct2D, DirectWrite, WIC, and WRL includes stay inside renderer implementation modules.
- Renderer does not own dashboard drawing modes, overlay policy, trace emission, diagnostics orchestration, or widget semantics.
