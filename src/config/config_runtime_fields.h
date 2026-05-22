#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "config/config.h"

enum class RuntimeConfigFieldValueKind : std::uint8_t {
    Int,
    Double,
    String,
    StringList,
    LogicalPoint,
    LogicalSize,
    HexColor,
    FontSpec,
    LayoutExpression,
};

enum class RuntimeConfigFieldPolicy : std::uint8_t {
    None,
    PositiveInt,
    NonNegativeInt,
    FontSize,
    Degrees,
};

struct RuntimeConfigFieldDescriptor {
    const char* key = "";
    std::uint32_t offset = 0;
    std::uint8_t keyLength = 0;
    RuntimeConfigFieldValueKind kind = RuntimeConfigFieldValueKind::String;
    RuntimeConfigFieldPolicy policy = RuntimeConfigFieldPolicy::None;
};

enum class RuntimeConfigSectionKind : std::uint8_t {
    Static,
    Dynamic,
    Custom,
};

enum class RuntimeConfigSectionCodec : std::uint8_t {
    Structured,
    Board,
    Metrics,
};

using RuntimeConfigDynamicItemVisitor = void (*)(void* context, std::string_view key, const void* item);
using RuntimeConfigEnsureDynamicItem = void* (*)(AppConfig & config, std::string_view key);
using RuntimeConfigFindDynamicItem = const void* (*)(const AppConfig& config, std::string_view key);
using RuntimeConfigForEachDynamicItem =
    void (*)(const AppConfig& config, void* context, RuntimeConfigDynamicItemVisitor visitor);

struct RuntimeConfigDynamicSectionCallbacks {
    RuntimeConfigEnsureDynamicItem ensure = nullptr;
    RuntimeConfigFindDynamicItem find = nullptr;
    RuntimeConfigForEachDynamicItem forEach = nullptr;
};

struct RuntimeConfigSectionDescriptor {
    const char* name = "";
    std::uint32_t rootOffset = 0;
    std::uint32_t keyOffset = 0;
    std::uint32_t itemSize = 0;
    std::uint8_t nameLength = 0;
    RuntimeConfigSectionKind kind = RuntimeConfigSectionKind::Static;
    RuntimeConfigSectionCodec codec = RuntimeConfigSectionCodec::Structured;
    const RuntimeConfigFieldDescriptor* fields = nullptr;
    std::uint32_t fieldCount = 0;
    RuntimeConfigDynamicSectionCallbacks dynamic;
};

std::string FormatLayoutExpression(const LayoutNodeConfig& node);
void DecodeRuntimeConfigField(const RuntimeConfigFieldDescriptor& field, void* owner, const std::string& value);
std::string EncodeRuntimeConfigField(const RuntimeConfigFieldDescriptor& field, const void* owner);
bool RuntimeConfigFieldEquals(const RuntimeConfigFieldDescriptor& field, const void* owner, const void* compareOwner);
// Implemented by generated file build/cmake/generated/config/config_meta.generated.cpp.
std::span<const RuntimeConfigSectionDescriptor> RuntimeConfigSectionDescriptors();
std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFields(const RuntimeConfigSectionDescriptor& section);
const RuntimeConfigSectionDescriptor* FindRuntimeConfigSection(std::string_view sectionName);
const RuntimeConfigSectionDescriptor* FindRuntimeConfigDynamicSection(std::string_view sectionName);
const RuntimeConfigSectionDescriptor* FindRuntimeConfigSectionByName(std::string_view sectionName);
