#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>

template <typename Enum> struct EnumStringTraits;

namespace enum_string_detail {

template <typename Enum> constexpr size_t EnumIndex(Enum value) {
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<size_t>(static_cast<Underlying>(value));
}

template <typename Enum, size_t N>
consteval bool HasDenseZeroBasedValues(const std::array<Enum, N>& values) {
    for (size_t index = 0; index < N; ++index) {
        if (EnumIndex(values[index]) != index) {
            return false;
        }
    }
    return true;
}

template <typename Enum, size_t N>
consteval bool HasUniqueValues(const std::array<Enum, N>& values) {
    for (size_t left = 0; left < N; ++left) {
        for (size_t right = left + 1; right < N; ++right) {
            if (values[left] == values[right]) {
                return false;
            }
        }
    }
    return true;
}

template <size_t N> consteval bool HasUniqueNames(const std::array<std::string_view, N>& names) {
    for (size_t left = 0; left < N; ++left) {
        for (size_t right = left + 1; right < N; ++right) {
            if (names[left] == names[right]) {
                return false;
            }
        }
    }
    return true;
}

template <typename Enum, size_t N>
consteval bool ValidateCanonicalMappings(
    const std::array<Enum, N>& values, const std::array<std::string_view, N>& names) {
    return HasDenseZeroBasedValues(values) && HasUniqueValues(values) && HasUniqueNames(names);
}

template <typename Enum> constexpr const auto& CanonicalNames() {
    using Traits = EnumStringTraits<Enum>;
    static_assert(std::is_enum_v<Enum>);
    static_assert(Traits::values.size() == Traits::names.size());
    static_assert(ValidateCanonicalMappings(Traits::values, Traits::names));
    return Traits::names;
}

template <typename Enum> const auto& CanonicalNameMap() {
    using Traits = EnumStringTraits<Enum>;
    static_assert(std::is_enum_v<Enum>);
    static_assert(Traits::values.size() == Traits::names.size());
    static_assert(ValidateCanonicalMappings(Traits::values, Traits::names));

    static const auto lookup = [] {
        std::unordered_map<std::string_view, Enum> map;
        map.reserve(Traits::names.size());
        for (size_t index = 0; index < Traits::names.size(); ++index) {
            map.emplace(Traits::names[index], Traits::values[index]);
        }
        return map;
    }();

    return lookup;
}

}  // namespace enum_string_detail

template <typename Enum> constexpr std::string_view EnumToString(Enum value) {
    const auto& names = enum_string_detail::CanonicalNames<Enum>();
    const size_t index = enum_string_detail::EnumIndex(value);
    if (index >= names.size()) {
        return {};
    }
    return names[index];
}

template <typename Enum> std::optional<Enum> EnumFromString(std::string_view text) {
    const auto& lookup = enum_string_detail::CanonicalNameMap<Enum>();
    const auto entry = lookup.find(text);
    if (entry == lookup.end()) {
        return std::nullopt;
    }
    return entry->second;
}

template <typename Enum> bool TryEnumFromString(std::string_view text, Enum& value) {
    const auto parsed = EnumFromString<Enum>(text);
    if (!parsed.has_value()) {
        return false;
    }
    value = *parsed;
    return true;
}
