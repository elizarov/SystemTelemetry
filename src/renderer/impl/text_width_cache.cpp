#include "renderer/impl/text_width_cache.h"

#include "util/strings.h"

namespace {

size_t TextWidthSlot(TextStyleId style, std::string_view text) {
    return (static_cast<size_t>(style) * 1315423911u) ^ StableStringHash(text);
}

}  // namespace

void RendererTextWidthCache::Clear() {
    for (Entry& entry : widths_) {
        entry.text.clear();
        entry.occupied = false;
    }
}

std::optional<int> RendererTextWidthCache::Find(TextStyleId style, std::string_view text) const {
    const Entry& entry = widths_[TextWidthSlot(style, text) % widths_.size()];
    if (entry.occupied && entry.style == style && std::string_view(entry.text) == text) {
        return entry.width;
    }
    return std::nullopt;
}

void RendererTextWidthCache::Store(TextStyleId style, std::string_view text, int width) {
    Entry& entry = widths_[TextWidthSlot(style, text) % widths_.size()];
    entry.style = style;
    entry.text.assign(text);
    entry.width = width;
    entry.occupied = true;
}
