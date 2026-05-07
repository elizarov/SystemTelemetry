#include "layout_edit/impl/layout_snap_solver.h"

#include <algorithm>
#include <utility>

namespace layout_snap_solver {

namespace {

struct CachedExtent {
    int firstWeight = 0;
    bool hasExtent = false;
    int extent = 0;
};

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

bool FindNearestSnapWeight(int currentWeight,
    int combinedWeight,
    int threshold,
    const std::vector<SnapCandidate>& candidates,
    const ExtentEvaluator& evaluateExtent,
    int& snappedWeight) {
    if (combinedWeight <= 1 || threshold <= 0) {
        return false;
    }

    std::vector<SnapCandidate> orderedCandidates = candidates;
    StableSortSnapCandidates(orderedCandidates);

    int currentExtent = 0;
    if (!evaluateExtent(currentWeight, currentExtent)) {
        return false;
    }

    for (const auto& candidate : orderedCandidates) {
        if (std::abs(currentExtent - candidate.targetExtent) > threshold) {
            continue;
        }

        std::vector<CachedExtent> extentCache;
        auto evaluateCached = [&](int firstWeight, int& extent) -> bool {
            const auto cached = std::find_if(extentCache.begin(), extentCache.end(), [firstWeight](const auto& entry) {
                return entry.firstWeight == firstWeight;
            });
            if (cached != extentCache.end()) {
                if (cached->hasExtent) {
                    extent = cached->extent;
                }
                return cached->hasExtent;
            }
            CachedExtent entry;
            entry.firstWeight = firstWeight;
            entry.hasExtent = evaluateExtent(firstWeight, entry.extent);
            if (entry.hasExtent) {
                extent = entry.extent;
            }
            extentCache.push_back(entry);
            return entry.hasExtent;
        };

        const int maxDistance = std::max(currentWeight - 1, (combinedWeight - 1) - currentWeight);
        for (int distance = 0; distance <= maxDistance; ++distance) {
            const int lowProbe = currentWeight - distance;
            if (lowProbe >= 1) {
                int lowExtent = 0;
                if (evaluateCached(lowProbe, lowExtent) && lowExtent == candidate.targetExtent) {
                    snappedWeight = lowProbe;
                    return true;
                }
            }

            const int highProbe = currentWeight + distance;
            if (distance > 0 && highProbe <= (combinedWeight - 1)) {
                int highExtent = 0;
                if (evaluateCached(highProbe, highExtent) && highExtent == candidate.targetExtent) {
                    snappedWeight = highProbe;
                    return true;
                }
            }
        }
    }

    return false;
}

}  // namespace layout_snap_solver
