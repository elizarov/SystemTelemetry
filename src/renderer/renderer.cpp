#include "renderer/renderer.h"

#include "renderer/impl/d2d_renderer.h"

std::unique_ptr<Renderer> CreateRenderer() {
    return std::make_unique<D2DRenderer>();
}

bool RenderBitmap::Empty() const {
    if (width <= 0 || height <= 0) {
        return true;
    }
    return resource == nullptr;
}

bool RenderBitmap::IsLiveLayer() const {
    return !Empty() && storage == RenderBitmapStorage::LiveLayer;
}
