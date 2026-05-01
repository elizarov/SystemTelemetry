#pragma once

#include <span>
#include <string>
#include <string_view>

#include "config/config.h"

struct RuntimeConfigFieldDescriptor {
    std::string_view key;
    void (*decode)(void* owner, const std::string& value);
    std::string (*encode)(const void* owner);
    bool (*equals)(const void* owner, const void* compareOwner);
};

std::string FormatLayoutExpression(const LayoutNodeConfig& node);

template <typename Section> std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFieldDescriptors();

#define SYSTEMTELEMETRY_CONFIG_FIELD_SECTIONS(X)                                                                       \
    X(DisplayConfig::Section)                                                                                          \
    X(NetworkConfig::Section)                                                                                          \
    X(StorageConfig::Section)                                                                                          \
    X(UiFontSetConfig::Section)                                                                                        \
    X(DashboardSectionConfig::Section)                                                                                 \
    X(CardStyleConfig::Section)                                                                                        \
    X(ColorsConfig::Section)                                                                                           \
    X(LayoutGuideSheetConfig::Section)                                                                                 \
    X(LayoutSectionConfig::Section)                                                                                    \
    X(LayoutCardConfig::Section)                                                                                       \
    X(MetricListWidgetConfig::Section)                                                                                 \
    X(DriveUsageListWidgetConfig::Section)                                                                             \
    X(ThroughputWidgetConfig::Section)                                                                                 \
    X(GaugeWidgetConfig::Section)                                                                                      \
    X(TextWidgetConfig::Section)                                                                                       \
    X(NetworkFooterWidgetConfig::Section)                                                                              \
    X(LayoutEditorConfig::Section)

#define SYSTEMTELEMETRY_DECLARE_RUNTIME_FIELDS(section_type)                                                           \
    template <> std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFieldDescriptors<section_type>();

SYSTEMTELEMETRY_CONFIG_FIELD_SECTIONS(SYSTEMTELEMETRY_DECLARE_RUNTIME_FIELDS)

#undef SYSTEMTELEMETRY_DECLARE_RUNTIME_FIELDS
