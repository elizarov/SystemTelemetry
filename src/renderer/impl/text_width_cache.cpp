#include "renderer/impl/text_width_cache.h"

namespace {

size_t HashTextWidthKey(TextStyleId style, std::string_view text) {
    return (std::hash<int>{}(static_cast<int>(style)) * 1315423911u) ^ std::hash<std::string_view>{}(text);
}

}  // namespace

void RendererTextWidthCache::Clear() {
    widths_.clear();
}

std::optional<int> RendererTextWidthCache::Find(TextStyleId style, std::string_view text) const {
    const LookupKey key{style, text};
    if (const auto it = widths_.find(key); it != widths_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void RendererTextWidthCache::Store(TextStyleId style, std::string_view text, int width) {
    widths_.emplace(Key{style, std::string(text)}, width);
}

size_t RendererTextWidthCache::KeyHash::operator()(const Key& key) const {
    return HashTextWidthKey(key.style, key.text);
}

size_t RendererTextWidthCache::KeyHash::operator()(const LookupKey& key) const {
    return HashTextWidthKey(key.style, key.text);
}

bool RendererTextWidthCache::KeyEqual::operator()(const Key& left, const Key& right) const {
    return left.style == right.style && left.text == right.text;
}

bool RendererTextWidthCache::KeyEqual::operator()(const Key& left, const LookupKey& right) const {
    return left.style == right.style && std::string_view(left.text) == right.text;
}

bool RendererTextWidthCache::KeyEqual::operator()(const LookupKey& left, const Key& right) const {
    return left.style == right.style && left.text == std::string_view(right.text);
}
