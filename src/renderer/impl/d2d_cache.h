#pragma once

#include <cstdint>
#include <d2d1.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

#include "renderer/impl/palette.h"
#include "renderer/render_types.h"

class D2DCache {
public:
    using IconSources = std::vector<std::pair<std::string, Microsoft::WRL::ComPtr<IWICBitmapSource>>>;

    void Clear();
    void ResetTarget();
    void AttachTarget(ID2D1RenderTarget* target);
    void ClearIconBitmaps();

    ID2D1SolidColorBrush* SolidBrush(ID2D1RenderTarget* target, RenderColor color);
    void DrawIcon(IWICImagingFactory* wicFactory,
        ID2D1RenderTarget* target,
        const IconSources& icons,
        std::string_view iconName,
        const RenderRect& rect);

private:
    struct IconCacheKey {
        std::string name;
        int width = 0;
        int height = 0;

        bool operator==(const IconCacheKey& other) const {
            return name == other.name && width == other.width && height == other.height;
        }
    };

    struct IconCacheKeyHash {
        size_t operator()(const IconCacheKey& key) const;
    };

    struct BrushCacheKey {
        std::uint32_t packedRgba = 0;

        bool operator==(const BrushCacheKey& other) const {
            return packedRgba == other.packedRgba;
        }
    };

    struct BrushCacheKeyHash {
        size_t operator()(const BrushCacheKey& key) const;
    };

    ID2D1RenderTarget* ownerTarget_ = nullptr;
    std::unordered_map<BrushCacheKey, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>, BrushCacheKeyHash> solidBrushes_;
    std::unordered_map<IconCacheKey, Microsoft::WRL::ComPtr<ID2D1Bitmap>, IconCacheKeyHash> iconBitmaps_;
};
