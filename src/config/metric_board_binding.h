#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kGpuTemperatureMetricId = "gpu.temp";
inline constexpr std::string_view kGpuFanMetricId = "gpu.fan";

enum class BoardMetricBindingKind {
    Temperature,
    Fan,
};

struct BoardMetricBindingTarget {
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string logicalName;
    bool fallback = false;
};

struct MetricBoardBindingUse {
    std::string metricId;
    BoardMetricBindingTarget target;
};

std::optional<BoardMetricBindingTarget> ResolveMetricBoardBindingTarget(std::string_view metricId);
bool ShouldExposeMetricBoardBinding(
    std::string_view metricId, const std::vector<MetricBoardBindingUse>& activeBindings);
