#pragma once

#include <cstdint>
#include <d2d1.h>
#include <unordered_map>
#include <wrl/client.h>

#include "renderer/impl/palette.h"

class D2DCache {
public:
    void Clear();
    void ResetTarget();
    void AttachTarget(ID2D1RenderTarget* target);

    ID2D1SolidColorBrush* SolidBrush(ID2D1RenderTarget* target, RenderColor color);

private:
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
};
