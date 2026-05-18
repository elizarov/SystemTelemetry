#include "config/metric_board_binding.h"

namespace {

constexpr std::string_view kBoardTemperatureMetricPrefix = "board.temp.";
constexpr std::string_view kBoardFanMetricPrefix = "board.fan.";

struct MetricFallbackBoardBinding {
    std::string_view metricId;
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string_view logicalName;
};

constexpr MetricFallbackBoardBinding kMetricFallbackBoardBindings[] = {
    {kGpuTemperatureMetricId, BoardMetricBindingKind::Temperature, "cpu"},
    {kGpuFanMetricId, BoardMetricBindingKind::Fan, "gpu"},
};

}  // namespace

std::optional<BoardMetricBindingTarget> ResolveMetricBoardBindingTarget(std::string_view metricId) {
    if (metricId.rfind(kBoardTemperatureMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Temperature,
            std::string(metricId.substr(kBoardTemperatureMetricPrefix.size())),
            false,
        };
    }
    if (metricId.rfind(kBoardFanMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Fan,
            std::string(metricId.substr(kBoardFanMetricPrefix.size())),
            false,
        };
    }
    for (const MetricFallbackBoardBinding& fallback : kMetricFallbackBoardBindings) {
        if (metricId == fallback.metricId) {
            return BoardMetricBindingTarget{
                fallback.kind,
                std::string(fallback.logicalName),
                true,
            };
        }
    }
    return std::nullopt;
}

bool ShouldExposeMetricBoardBinding(
    std::string_view metricId, const std::vector<MetricBoardBindingUse>& activeBindings) {
    const auto target = ResolveMetricBoardBindingTarget(metricId);
    if (!target.has_value()) {
        return false;
    }
    if (!target->fallback) {
        return true;
    }
    for (const MetricBoardBindingUse& activeBinding : activeBindings) {
        if (activeBinding.metricId == metricId && activeBinding.target.kind == target->kind &&
            activeBinding.target.logicalName == target->logicalName) {
            return true;
        }
    }
    return false;
}
