#pragma once

#include <array>
#include <cstddef>
#include <d2d1.h>
#include <wrl/client.h>

#include "renderer/impl/palette.h"

class D2DCache {
public:
    void Clear();
    void ResetTarget();
    void AttachTarget(ID2D1RenderTarget* target);

    ID2D1SolidColorBrush* SolidBrush(ID2D1RenderTarget* target, const RendererPalette& palette, RenderColorId colorId);

private:
    static constexpr std::size_t kColorCount = static_cast<std::size_t>(RenderColorId::Count);

    ID2D1RenderTarget* ownerTarget_ = nullptr;
    std::array<Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>, kColorCount> solidBrushes_{};
};
