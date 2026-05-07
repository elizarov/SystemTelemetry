#pragma once

#include <cstddef>
#include <vector>

#include "util/function_ref.h"

namespace layout_snap_solver {

struct SnapCandidate {
    int targetExtent = 0;
    int startDistance = 0;
    size_t groupOrder = 0;
};

using ExtentEvaluator = FunctionRef<bool(int firstWeight, int& extent)>;

bool FindNearestSnapWeight(int currentWeight,
    int combinedWeight,
    int threshold,
    const std::vector<SnapCandidate>& candidates,
    const ExtentEvaluator& evaluateExtent,
    int& snappedWeight);

}  // namespace layout_snap_solver
