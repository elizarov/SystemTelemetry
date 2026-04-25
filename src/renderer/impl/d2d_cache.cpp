#include "renderer/impl/d2d_cache.h"

#include <algorithm>

#include "renderer/impl/d2d_render_conversions.h"

size_t D2DCache::IconCacheKeyHash::operator()(const IconCacheKey& key) const {
    size_t hash = std::hash<std::string>{}(key.name);
    hash = (hash * 1315423911u) ^ std::hash<int>{}(key.width);
    hash = (hash * 1315423911u) ^ std::hash<int>{}(key.height);
    return hash;
}

size_t D2DCache::BrushCacheKeyHash::operator()(const BrushCacheKey& key) const {
    return std::hash<std::uint32_t>{}(key.packedRgba);
}

void D2DCache::Clear() {
    solidBrushes_.clear();
    iconBitmaps_.clear();
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

void D2DCache::ClearIconBitmaps() {
    iconBitmaps_.clear();
}

ID2D1SolidColorBrush* D2DCache::SolidBrush(ID2D1RenderTarget* target, RenderColor color) {
    if (target == nullptr) {
        return nullptr;
    }
    const BrushCacheKey key{color.PackedRgba()};
    if (const auto it = solidBrushes_.find(key); it != solidBrushes_.end()) {
        return it->second.Get();
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(color.ToD2DColorF(), brush.GetAddressOf())) || brush == nullptr) {
        return nullptr;
    }
    return solidBrushes_.emplace(key, std::move(brush)).first->second.Get();
}

void D2DCache::DrawIcon(IWICImagingFactory* wicFactory,
    ID2D1RenderTarget* target,
    const IconSources& icons,
    std::string_view iconName,
    const RenderRect& rect) {
    const auto it =
        std::find_if(icons.begin(), icons.end(), [&](const auto& entry) { return entry.first == iconName; });
    if (it == icons.end() || it->second == nullptr || target == nullptr) {
        return;
    }
    const int width = (std::max)(0, rect.Width());
    const int height = (std::max)(0, rect.Height());
    if (width <= 0 || height <= 0) {
        return;
    }

    const IconCacheKey cacheKey{std::string(iconName), width, height};
    auto scaled = iconBitmaps_.find(cacheKey);
    if (scaled == iconBitmaps_.end()) {
        if (wicFactory == nullptr) {
            return;
        }

        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        HRESULT hr = wicFactory->CreateBitmapScaler(scaler.GetAddressOf());
        if (FAILED(hr) || scaler == nullptr) {
            return;
        }

        hr = scaler->Initialize(
            it->second.Get(), static_cast<UINT>(width), static_cast<UINT>(height), WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            return;
        }

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        hr = target->CreateBitmapFromWicBitmap(scaler.Get(), bitmap.GetAddressOf());
        if (FAILED(hr) || bitmap == nullptr) {
            return;
        }
        scaled = iconBitmaps_.emplace(cacheKey, std::move(bitmap)).first;
    }

    target->DrawBitmap(scaled->second.Get(), D2DRectFromRenderRect(rect), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
