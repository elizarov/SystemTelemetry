#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "dashboard_renderer/render_types.h"

class DashboardTextWidthCache {
public:
    void Clear();

    std::optional<int> Find(TextStyleId style, std::string_view text) const;
    void Store(TextStyleId style, std::string_view text, int width);

private:
    struct Key {
        TextStyleId style = TextStyleId::Text;
        std::string text;

        bool operator==(const Key& other) const {
            return style == other.style && text == other.text;
        }
    };

    struct LookupKey {
        TextStyleId style = TextStyleId::Text;
        std::string_view text;
    };

    struct KeyHash {
        using is_transparent = void;

        size_t operator()(const Key& key) const;
        size_t operator()(const LookupKey& key) const;
    };

    struct KeyEqual {
        using is_transparent = void;

        bool operator()(const Key& left, const Key& right) const;
        bool operator()(const Key& left, const LookupKey& right) const;
        bool operator()(const LookupKey& left, const Key& right) const;
    };

    std::unordered_map<Key, int, KeyHash, KeyEqual> widths_;
};
