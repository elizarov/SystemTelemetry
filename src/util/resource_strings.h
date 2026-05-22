#pragma once

#include <cstddef>
#include <cstdint>

enum class ResourceStringId : std::uint32_t {};

static_assert(sizeof(ResourceStringId) == sizeof(std::uint32_t));

const char* ResourceStringText(ResourceStringId id);

#include "resource_strings.generated.h"

namespace resource_strings_detail {

constexpr std::uint32_t ResourceStringHashPrime = 16777619u;

std::uint32_t ResourceStringHash(const char* text, std::size_t length);

}  // namespace resource_strings_detail

template <std::size_t Size> consteval ResourceStringId RES_STR(const char (&text)[Size]) {
    std::uint32_t hash = resource_strings_detail::ResourceStringHashSeed;
    for (std::size_t index = 0; index < Size - 1; ++index) {
        hash ^= static_cast<std::uint8_t>(text[index]);
        hash *= resource_strings_detail::ResourceStringHashPrime;
    }
    return static_cast<ResourceStringId>(hash);
}
