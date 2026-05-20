#include "dashboard_renderer/impl/metric_lookup_cache.h"

#include "config/config_telemetry.h"
#include "telemetry/metrics.h"
#include "util/strings.h"

namespace {

constexpr size_t kMetricLookupCacheSlots = 128;

size_t MetricLookupCacheSlot(std::string_view key) {
    return StableStringHash(key) % kMetricLookupCacheSlots;
}

}  // namespace

const MetricDefinitionConfig* MetricLookupCache::FindDefinition(
    const MetricsSectionConfig& metrics, std::string_view metricRef) const {
    MetricDefinitionCacheEntry& entry = definitions_[MetricLookupCacheSlot(metricRef)];
    if (entry.occupied && std::string_view(entry.key) == metricRef) {
        return entry.definition;
    }
    const MetricDefinitionConfig* definition = FindEffectiveMetricDefinition(metrics, metricRef);
    entry.key.assign(metricRef);
    entry.definition = definition;
    entry.occupied = true;
    return definition;
}

const std::string& MetricLookupCache::ResolveSampleValueText(
    const MetricsSectionConfig& metrics, std::string_view metricRef) const {
    MetricSampleValueTextCacheEntry& entry = sampleValueTexts_[MetricLookupCacheSlot(metricRef)];
    if (entry.occupied && std::string_view(entry.key) == metricRef) {
        return entry.text;
    }
    entry.key.assign(metricRef);
    entry.text = ResolveMetricSampleValueText(metrics, entry.key);
    entry.occupied = true;
    return entry.text;
}

void MetricLookupCache::Clear() {
    for (MetricDefinitionCacheEntry& entry : definitions_) {
        entry.key.clear();
        entry.definition = nullptr;
        entry.occupied = false;
    }
    for (MetricSampleValueTextCacheEntry& entry : sampleValueTexts_) {
        entry.key.clear();
        entry.text.clear();
        entry.occupied = false;
    }
}
