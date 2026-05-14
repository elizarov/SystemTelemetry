#pragma once

#include <chrono>
#include <unordered_map>
#include <vector>

#include "telemetry/telemetry.h"
#include "widget/animation_types.h"

class DashboardAnimationTimeline {
public:
    using Clock = std::chrono::steady_clock;

    explicit DashboardAnimationTimeline(std::chrono::milliseconds duration = kTelemetryRefreshInterval);

    void BeginFrame(Clock::time_point now);
    ScalarFillSample ResolveScalar(const AnimationDataKey& key, const ScalarFillSample& target);
    ThroughputChartSample ResolveThroughput(const AnimationDataKey& key, const ThroughputChartSample& target);
    void EndFrame();
    void Reset();
    bool HasActiveAnimations(Clock::time_point now) const;

private:
    struct ScalarTrack {
        ScalarFillSample start;
        ScalarFillSample target;
        Clock::time_point startTime{};
        bool touched = false;
    };

    struct ThroughputTrack {
        ThroughputChartSample start;
        ThroughputChartSample target;
        std::vector<double> scrollSamples;
        double targetPlotShiftSamples = 0.0;
        Clock::time_point startTime{};
        bool touched = false;
    };

    double ProgressSince(Clock::time_point startTime, Clock::time_point now) const;
    ScalarFillSample SampleScalar(const ScalarTrack& track, Clock::time_point now) const;
    ThroughputChartSample SampleThroughput(const ThroughputTrack& track, Clock::time_point now) const;

    std::unordered_map<AnimationDataKey, ScalarTrack, AnimationDataKeyHash> scalarTracks_;
    std::unordered_map<AnimationDataKey, ThroughputTrack, AnimationDataKeyHash> throughputTracks_;
    std::chrono::milliseconds duration_;
    Clock::time_point frameTime_{};
    bool frameActive_ = false;
};
