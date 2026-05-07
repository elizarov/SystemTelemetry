#pragma once

#include <optional>
#include <string>
#include <string_view>

enum class BoardMetricBindingKind {
    Temperature,
    Fan,
};

struct BoardMetricBindingTarget {
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string logicalName;
};

std::optional<BoardMetricBindingTarget> ParseBoardMetricBindingTarget(std::string_view metricId);
