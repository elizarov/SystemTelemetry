#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace layout_snap_solver {

struct SnapCandidate {
    int targetExtent = 0;
    int startDistance = 0;
    size_t groupOrder = 0;
};

using ExtentEvaluator = std::function<std::optional<int>(int firstWeight)>;

std::optional<int> FindNearestSnapWeight(int currentWeight, int combinedWeight, int threshold,
    const std::vector<SnapCandidate>& candidates, const ExtentEvaluator& evaluateExtent);

}  // namespace layout_snap_solver
