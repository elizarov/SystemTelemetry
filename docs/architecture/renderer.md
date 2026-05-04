# Renderer Package

`src/renderer/` owns render-space contract types, renderer style DTOs, the D2D-free `Renderer` interface, renderer style resources, render-target lifecycle, and the Direct2D/DirectWrite/WIC implementation under `src/renderer/impl/`.

## Responsibilities

- Provide primitive drawing for text, rectangles, rounded rectangles, ellipses, lines, arcs, polylines, filled paths, clips, and translations.
- Own live HWND rendering, text measurement, shared offscreen bitmap rendering for screenshot export and validation priming, and icon loading.
- Keep resolved RGBA palettes private and map render color ids internally.
- Decode embedded panel icons through WIC and upload render-target-local Direct2D bitmaps as needed.
- Use non-owning `FunctionRef` views for synchronous draw and measurement callbacks; callback storage remains owned by the caller.

## Boundaries

- `renderer` may depend on `renderer`, `config`, `util`, and the synthetic `d2d` package.
- The public renderer interface is D2D-free. Direct2D, DirectWrite, WIC, and WRL includes stay inside renderer implementation modules.
- Renderer does not own dashboard drawing modes, overlay policy, trace emission, diagnostics orchestration, or widget semantics.
