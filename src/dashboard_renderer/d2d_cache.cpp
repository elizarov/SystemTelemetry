#include "dashboard_renderer/d2d_cache.h"

#include <algorithm>

size_t DashboardD2DCache::PanelIconCacheKeyHash::operator()(const PanelIconCacheKey& key) const {
    size_t hash = std::hash<std::string>{}(key.name);
    hash = (hash * 1315423911u) ^ std::hash<int>{}(key.width);
    hash = (hash * 1315423911u) ^ std::hash<int>{}(key.height);
    return hash;
}

size_t DashboardD2DCache::BrushCacheKeyHash::operator()(const BrushCacheKey& key) const {
    return std::hash<std::uint32_t>{}(key.packedRgba);
}

void DashboardD2DCache::Clear() {
    solidBrushes_.clear();
    panelIconBitmaps_.clear();
}

void DashboardD2DCache::ResetTarget() {
    ownerTarget_ = nullptr;
    Clear();
}

void DashboardD2DCache::AttachTarget(ID2D1RenderTarget* target) {
    if (ownerTarget_ == target) {
        return;
    }
    Clear();
    ownerTarget_ = target;
}

void DashboardD2DCache::ClearPanelIconBitmaps() {
    panelIconBitmaps_.clear();
}

ID2D1SolidColorBrush* DashboardD2DCache::SolidBrush(ID2D1RenderTarget* target, RenderColor color) {
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

void DashboardD2DCache::DrawPanelIcon(IWICImagingFactory* wicFactory,
    ID2D1RenderTarget* target,
    const PanelIconSources& panelIcons,
    const std::string& iconName,
    const RenderRect& iconRect) {
    const auto it =
        std::find_if(panelIcons.begin(), panelIcons.end(), [&](const auto& entry) { return entry.first == iconName; });
    if (it == panelIcons.end() || it->second == nullptr || target == nullptr) {
        return;
    }
    const int width = (std::max)(0, iconRect.Width());
    const int height = (std::max)(0, iconRect.Height());
    if (width <= 0 || height <= 0) {
        return;
    }

    const PanelIconCacheKey cacheKey{iconName, width, height};
    auto scaled = panelIconBitmaps_.find(cacheKey);
    if (scaled == panelIconBitmaps_.end()) {
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
        scaled = panelIconBitmaps_.emplace(cacheKey, std::move(bitmap)).first;
    }

    target->DrawBitmap(scaled->second.Get(),
        D2D1::RectF(static_cast<float>(iconRect.left),
            static_cast<float>(iconRect.top),
            static_cast<float>(iconRect.right),
            static_cast<float>(iconRect.bottom)),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
