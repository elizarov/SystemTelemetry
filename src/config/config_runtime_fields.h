#pragma once

#include <cstdint>
#include <span>
#include <string>

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

std::string FormatLayoutExpression(const LayoutNodeConfig& node);
void DecodeRuntimeConfigField(const RuntimeConfigFieldDescriptor& field, void* owner, const std::string& value);
std::string EncodeRuntimeConfigField(const RuntimeConfigFieldDescriptor& field, const void* owner);
bool RuntimeConfigFieldEquals(const RuntimeConfigFieldDescriptor& field, const void* owner, const void* compareOwner);

template <typename Section> std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFieldDescriptors();

#define CASEDASH_CONFIG_FIELD_SECTIONS(X)                                                                              \
    X(DisplayConfig::Section)                                                                                          \
    X(GpuConfig::Section)                                                                                              \
    X(NetworkConfig::Section)                                                                                          \
    X(StorageConfig::Section)                                                                                          \
    X(UiFontSetConfig::Section)                                                                                        \
    X(DashboardSectionConfig::Section)                                                                                 \
    X(CardStyleConfig::Section)                                                                                        \
    X(ColorsConfig::Section)                                                                                           \
    X(LayoutGuideSheetConfig::Section)                                                                                 \
    X(ThemeConfig::Section)                                                                                            \
    X(LayoutSectionConfig::Section)                                                                                    \
    X(LayoutCardConfig::Section)                                                                                       \
    X(MetricListWidgetConfig::Section)                                                                                 \
    X(DriveUsageListWidgetConfig::Section)                                                                             \
    X(ThroughputWidgetConfig::Section)                                                                                 \
    X(GaugeWidgetConfig::Section)                                                                                      \
    X(TextWidgetConfig::Section)                                                                                       \
    X(NetworkFooterWidgetConfig::Section)                                                                              \
    X(LayoutEditorConfig::Section)

#define CASEDASH_DECLARE_RUNTIME_FIELDS(section_type)                                                                  \
    template <> std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFieldDescriptors<section_type>();

CASEDASH_CONFIG_FIELD_SECTIONS(CASEDASH_DECLARE_RUNTIME_FIELDS)

#undef CASEDASH_DECLARE_RUNTIME_FIELDS
