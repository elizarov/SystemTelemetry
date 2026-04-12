#pragma once

#include <string>

#include "config.h"
#include "telemetry.h"

class RetainedHistoryStore {
public:
    void Reset(SystemSnapshot& snapshot) const;
    void PushSample(SystemSnapshot& snapshot, const std::string& seriesRef, double value) const;
    void PushBoardMetricSamples(SystemSnapshot& snapshot, const MetricScaleConfig& scales) const;
};

void RebuildRetainedHistoryIndex(SystemSnapshot& snapshot);
