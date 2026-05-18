#pragma once

#include <cstdint>

enum class ResourceStringId : std::uint32_t {};

static_assert(sizeof(ResourceStringId) == sizeof(std::uint32_t));

const char* ResourceStringText(ResourceStringId id);

#include "resource_strings.generated.h"
