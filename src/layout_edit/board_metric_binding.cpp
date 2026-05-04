#include "layout_edit/board_metric_binding.h"

namespace {

constexpr std::string_view kBoardTemperatureMetricPrefix = "board.temp.";
constexpr std::string_view kBoardFanMetricPrefix = "board.fan.";

}  // namespace

std::optional<BoardMetricBindingTarget> ParseBoardMetricBindingTarget(std::string_view metricId) {
    // Size: one parser is shared by shell preview code and dialog controls instead of duplicated inline copies.
    if (metricId.rfind(kBoardTemperatureMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Temperature,
            std::string(metricId.substr(kBoardTemperatureMetricPrefix.size())),
        };
    }
    if (metricId.rfind(kBoardFanMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Fan,
            std::string(metricId.substr(kBoardFanMetricPrefix.size())),
        };
    }
    return std::nullopt;
}
