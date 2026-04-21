#pragma once

#include <string>

#include "telemetry/telemetry.h"

class RetainedHistoryStore {
public:
    void Reset(SystemSnapshot& snapshot) const;
    void PushSample(SystemSnapshot& snapshot, const std::string& seriesRef, double value) const;
    void PushBoardMetricSamples(SystemSnapshot& snapshot) const;
};

void RebuildRetainedHistoryIndex(SystemSnapshot& snapshot);
