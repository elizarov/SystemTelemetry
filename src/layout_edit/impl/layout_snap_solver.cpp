#include "layout_edit/impl/layout_snap_solver.h"

#include <algorithm>
#include <map>

namespace layout_snap_solver {

std::optional<int> FindNearestSnapWeight(int currentWeight,
    int combinedWeight,
    int threshold,
    const std::vector<SnapCandidate>& candidates,
    const ExtentEvaluator& evaluateExtent) {
    if (combinedWeight <= 1 || threshold <= 0 || !evaluateExtent) {
        return std::nullopt;
    }

    std::vector<SnapCandidate> orderedCandidates = candidates;
    std::stable_sort(orderedCandidates.begin(), orderedCandidates.end(), [](const auto& left, const auto& right) {
        if (left.startDistance != right.startDistance) {
            return left.startDistance < right.startDistance;
        }
        return left.groupOrder < right.groupOrder;
    });

    const std::optional<int> currentExtent = evaluateExtent(currentWeight);
    if (!currentExtent.has_value()) {
        return std::nullopt;
    }

    for (const auto& candidate : orderedCandidates) {
        if (std::abs(*currentExtent - candidate.targetExtent) > threshold) {
            continue;
        }

        std::map<int, std::optional<int>> extentCache;
        auto evaluateCached = [&](int firstWeight) -> std::optional<int> {
            const auto cached = extentCache.find(firstWeight);
            if (cached != extentCache.end()) {
                return cached->second;
            }
            std::optional<int> extent = evaluateExtent(firstWeight);
            extentCache.emplace(firstWeight, extent);
            return extent;
        };

        const int maxDistance = std::max(currentWeight - 1, (combinedWeight - 1) - currentWeight);
        for (int distance = 0; distance <= maxDistance; ++distance) {
            const int lowProbe = currentWeight - distance;
            if (lowProbe >= 1) {
                const std::optional<int> lowExtent = evaluateCached(lowProbe);
                if (lowExtent.has_value() && *lowExtent == candidate.targetExtent) {
                    return lowProbe;
                }
            }

            const int highProbe = currentWeight + distance;
            if (distance > 0 && highProbe <= (combinedWeight - 1)) {
                const std::optional<int> highExtent = evaluateCached(highProbe);
                if (highExtent.has_value() && *highExtent == candidate.targetExtent) {
                    return highProbe;
                }
            }
        }
    }

    return std::nullopt;
}

}  // namespace layout_snap_solver
