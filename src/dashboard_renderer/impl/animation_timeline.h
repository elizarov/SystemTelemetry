#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "telemetry/telemetry.h"
#include "widget/animation.h"

class DashboardAnimationTimeline {
public:
    using Clock = std::chrono::steady_clock;

    explicit DashboardAnimationTimeline(std::chrono::milliseconds duration = kTelemetryRefreshInterval);

    void BeginFrame(Clock::time_point now);
    WidgetAnimationStatePtr Resolve(
        const AnimationDataKey& key, const WidgetAnimationState& target, std::uint64_t targetVersion);
    void EndFrame();
    void Reset();
    bool HasActiveAnimations(Clock::time_point now) const;

private:
    struct Track {
        WidgetAnimationStatePtr start;
        WidgetAnimationStatePtr target;
        WidgetAnimationTransitionPtr transition;
        const WidgetAnimationState* observedTarget = nullptr;
        std::uint64_t observedTargetVersion = 0;
        Clock::time_point startTime{};
        bool touched = false;
    };

    double ProgressSince(Clock::time_point startTime, Clock::time_point now) const;
    WidgetAnimationStatePtr SampleTrack(const Track& track, Clock::time_point now) const;

    std::unordered_map<AnimationDataKey, Track, AnimationDataKeyHash> tracks_;
    std::chrono::milliseconds duration_;
    Clock::time_point frameTime_{};
    bool frameActive_ = false;
};
