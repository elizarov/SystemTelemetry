#include "renderer/impl/d2d_cache.h"

#include <utility>

void D2DCache::Clear() {
    for (Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush : solidBrushes_) {
        brush.Reset();
    }
}

void D2DCache::ResetTarget() {
    ownerTarget_ = nullptr;
    Clear();
}

void D2DCache::AttachTarget(ID2D1RenderTarget* target) {
    if (ownerTarget_ == target) {
        return;
    }
    Clear();
    ownerTarget_ = target;
}

ID2D1SolidColorBrush* D2DCache::SolidBrush(
    ID2D1RenderTarget* target, const RendererPalette& palette, RenderColorId colorId) {
    if (target == nullptr) {
        return nullptr;
    }
    const std::size_t slot = static_cast<std::size_t>(colorId);
    if (slot >= solidBrushes_.size()) {
        return nullptr;
    }
    if (solidBrushes_[slot] != nullptr) {
        return solidBrushes_[slot].Get();
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    const RenderColor& color = palette.Get(colorId);
    if (FAILED(target->CreateSolidColorBrush(color.ToD2DColorF(), brush.GetAddressOf())) || brush == nullptr) {
        return nullptr;
    }
    ID2D1SolidColorBrush* result = brush.Get();
    solidBrushes_[slot] = std::move(brush);
    return result;
}
