#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "renderer/render_types.h"

class RendererTextWidthCache {
public:
    void Clear();

    std::optional<int> Find(TextStyleId style, std::string_view text) const;
    void Store(TextStyleId style, std::string_view text, int width);

private:
    struct Entry {
        TextStyleId style = TextStyleId::Text;
        std::string text;
        int width = 0;
        bool occupied = false;
    };

    static constexpr size_t kSlotCount = 256;

    std::array<Entry, kSlotCount> widths_{};
};
