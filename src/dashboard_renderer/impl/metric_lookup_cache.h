#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

struct MetricDefinitionConfig;
struct MetricsSectionConfig;

class MetricLookupCache {
public:
    const MetricDefinitionConfig* FindDefinition(const MetricsSectionConfig& metrics, std::string_view metricRef) const;
    const std::string& ResolveSampleValueText(const MetricsSectionConfig& metrics, std::string_view metricRef) const;
    void Clear();

private:
    struct MetricDefinitionCacheEntry {
        std::string key;
        const MetricDefinitionConfig* definition = nullptr;
        bool occupied = false;
    };

    struct MetricSampleValueTextCacheEntry {
        std::string key;
        std::string text;
        bool occupied = false;
    };

    static constexpr size_t kSlots = 128;

    mutable std::array<MetricDefinitionCacheEntry, kSlots> definitions_{};
    mutable std::array<MetricSampleValueTextCacheEntry, kSlots> sampleValueTexts_{};
};
