#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemetry/timing.h"
#include "widget/animation.h"

class DashboardAnimationTimeline {
public:
    using Clock = std::chrono::steady_clock;
    enum class TrackRetention {
        PruneUntouched,
        KeepUntouched,
    };

    explicit DashboardAnimationTimeline(std::chrono::milliseconds duration = kTelemetryRefreshInterval);

    void BeginFrame(Clock::time_point now);
    WidgetAnimationStatePtr Resolve(
        const AnimationDataKey& key, const WidgetAnimationState& target, std::uint64_t targetVersion);
    std::size_t EndFrame(TrackRetention retention = TrackRetention::PruneUntouched);
    void Reset();
    std::size_t TrackCount() const;
    bool HasActiveAnimations(Clock::time_point now) const;

private:
    struct Track {
        WidgetAnimationStatePtr start;
        WidgetAnimationStatePtr target;
        WidgetAnimationTransitionPtr transition;
        // Guards timeline retargeting against repeated resolutions for the same metric target.
        std::uint64_t observedTargetVersion = 0;
        Clock::time_point startTime{};
        bool touched = false;
    };

    struct TrackEntry {
        AnimationDataKey key;
        Track track;
    };

    double ProgressSince(Clock::time_point startTime, Clock::time_point now) const;
    WidgetAnimationStatePtr SampleTrack(const Track& track, Clock::time_point now) const;

    // Size: active animation sets are tiny; flat scans avoid unordered_map machinery in the shipped app.
    std::vector<TrackEntry> tracks_;
    std::chrono::milliseconds duration_;
    Clock::time_point frameTime_{};
    bool frameActive_ = false;
};
