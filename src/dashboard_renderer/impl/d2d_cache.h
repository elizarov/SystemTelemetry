#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "dashboard_renderer/impl/palette.h"
#include "dashboard_renderer/render_types.h"

class DashboardD2DCache {
public:
    using PanelIconSources = std::vector<std::pair<std::string, Microsoft::WRL::ComPtr<IWICBitmapSource>>>;

    void Clear();
    void ResetTarget();
    void AttachTarget(ID2D1RenderTarget* target);
    void ClearPanelIconBitmaps();

    ID2D1SolidColorBrush* SolidBrush(ID2D1RenderTarget* target, RenderColor color);
    void DrawPanelIcon(IWICImagingFactory* wicFactory,
        ID2D1RenderTarget* target,
        const PanelIconSources& panelIcons,
        const std::string& iconName,
        const RenderRect& iconRect);

private:
    struct PanelIconCacheKey {
        std::string name;
        int width = 0;
        int height = 0;

        bool operator==(const PanelIconCacheKey& other) const {
            return name == other.name && width == other.width && height == other.height;
        }
    };

    struct PanelIconCacheKeyHash {
        size_t operator()(const PanelIconCacheKey& key) const;
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
    std::unordered_map<PanelIconCacheKey, Microsoft::WRL::ComPtr<ID2D1Bitmap>, PanelIconCacheKeyHash> panelIconBitmaps_;
};
