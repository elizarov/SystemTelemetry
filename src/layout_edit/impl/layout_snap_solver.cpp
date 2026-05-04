#include "layout_edit/impl/layout_snap_solver.h"

#include <algorithm>
#include <utility>

namespace layout_snap_solver {

namespace {

bool SnapCandidateLess(const SnapCandidate& left, const SnapCandidate& right) {
    if (left.startDistance != right.startDistance) {
        return left.startDistance < right.startDistance;
    }
    return left.groupOrder < right.groupOrder;
}

void StableSortSnapCandidates(std::vector<SnapCandidate>& candidates) {
    // Size: snap candidate lists are tiny; insertion sort avoids std::stable_sort template code.
    for (size_t i = 1; i < candidates.size(); ++i) {
        SnapCandidate current = std::move(candidates[i]);
        size_t j = i;
        while (j > 0 && SnapCandidateLess(current, candidates[j - 1])) {
            candidates[j] = std::move(candidates[j - 1]);
            --j;
        }
        candidates[j] = std::move(current);
    }
}

}  // namespace

std::optional<int> FindNearestSnapWeight(int currentWeight,
    int combinedWeight,
    int threshold,
    const std::vector<SnapCandidate>& candidates,
    const ExtentEvaluator& evaluateExtent) {
    if (combinedWeight <= 1 || threshold <= 0) {
        return std::nullopt;
    }

    std::vector<SnapCandidate> orderedCandidates = candidates;
    StableSortSnapCandidates(orderedCandidates);

    const std::optional<int> currentExtent = evaluateExtent(currentWeight);
    if (!currentExtent.has_value()) {
        return std::nullopt;
    }

    for (const auto& candidate : orderedCandidates) {
        if (std::abs(*currentExtent - candidate.targetExtent) > threshold) {
            continue;
        }

        std::vector<std::pair<int, std::optional<int>>> extentCache;
        auto evaluateCached = [&](int firstWeight) -> std::optional<int> {
            const auto cached = std::find_if(extentCache.begin(), extentCache.end(), [firstWeight](const auto& entry) {
                return entry.first == firstWeight;
            });
            if (cached != extentCache.end()) {
                return cached->second;
            }
            std::optional<int> extent = evaluateExtent(firstWeight);
            extentCache.emplace_back(firstWeight, extent);
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
