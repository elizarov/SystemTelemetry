#include "renderer/renderer.h"

#include "renderer/impl/d2d_renderer.h"

std::unique_ptr<Renderer> CreateRenderer(Trace& trace) {
    return std::make_unique<D2DRenderer>(trace);
}
