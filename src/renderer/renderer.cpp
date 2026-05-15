#include "renderer/renderer.h"

#include "renderer/impl/d2d_renderer.h"

std::unique_ptr<Renderer> CreateRenderer() {
    return std::make_unique<D2DRenderer>();
}

bool RenderBitmap::Empty() const {
    if (width <= 0 || height <= 0) {
        return true;
    }
    if (resource != nullptr) {
        return false;
    }
    if (stride <= 0 || bgra.empty()) {
        return true;
    }
    const auto minimumStride = static_cast<std::size_t>(width) * 4;
    const auto minimumSize = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    return static_cast<std::size_t>(stride) < minimumStride || bgra.size() < minimumSize;
}
