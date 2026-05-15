#include "dashboard_renderer/impl/animation_timeline.h"

#include <algorithm>
#include <utility>

DashboardAnimationTimeline::DashboardAnimationTimeline(std::chrono::milliseconds duration)
    : duration_(duration.count() > 0 ? duration : std::chrono::milliseconds(1)) {}

void DashboardAnimationTimeline::BeginFrame(Clock::time_point now) {
    frameTime_ = now;
    frameActive_ = true;
    for (auto& entry : tracks_) {
        entry.second.touched = false;
    }
}

WidgetAnimationStatePtr DashboardAnimationTimeline::Resolve(
    const AnimationDataKey& key, const WidgetAnimationState& target, std::uint64_t targetVersion) {
    if (!frameActive_) {
        return target.Clone();
    }

    auto it = tracks_.find(key);
    if (it == tracks_.end()) {
        Track track;
        track.start = target.InitialState();
        track.target = target.Clone();
        track.transition = track.target->TransitionFrom(*track.start);
        track.observedTargetVersion = targetVersion;
        track.startTime = frameTime_;
        track.touched = true;
        it = tracks_.insert({key, std::move(track)}).first;
    } else {
        Track& track = it->second;
        const bool sameStateType = track.target != nullptr && track.target->TypeToken() == target.TypeToken();
        const bool sameVersionTarget = sameStateType && track.observedTargetVersion == targetVersion;
        if (!sameVersionTarget && (!sameStateType || !track.target->Equals(target))) {
            track.start = sameStateType ? target.RetargetStart(*SampleTrack(track, frameTime_)) : target.InitialState();
            track.target = target.Clone();
            track.transition = track.target->TransitionFrom(*track.start);
            track.startTime = frameTime_;
        }
        track.observedTargetVersion = targetVersion;
        track.touched = true;
    }

    return SampleTrack(it->second, frameTime_);
}

std::size_t DashboardAnimationTimeline::EndFrame(TrackRetention retention) {
    std::size_t prunedCount = 0;
    if (retention == TrackRetention::PruneUntouched) {
        for (auto it = tracks_.begin(); it != tracks_.end();) {
            if (!it->second.touched) {
                it = tracks_.erase(it);
                ++prunedCount;
            } else {
                ++it;
            }
        }
    }
    frameActive_ = false;
    return prunedCount;
}

void DashboardAnimationTimeline::Reset() {
    tracks_.clear();
    frameActive_ = false;
}

std::size_t DashboardAnimationTimeline::TrackCount() const {
    return tracks_.size();
}

bool DashboardAnimationTimeline::HasActiveAnimations(Clock::time_point now) const {
    for (const auto& entry : tracks_) {
        const Track& track = entry.second;
        if (track.transition != nullptr && track.transition->HasActiveChange() &&
            ProgressSince(track.startTime, now) < 1.0) {
            return true;
        }
    }
    return false;
}

double DashboardAnimationTimeline::ProgressSince(Clock::time_point startTime, Clock::time_point now) const {
    if (now <= startTime) {
        return 0.0;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - startTime);
    const std::chrono::duration<double> duration(duration_);
    return std::clamp(elapsed.count() / duration.count(), 0.0, 1.0);
}

WidgetAnimationStatePtr DashboardAnimationTimeline::SampleTrack(const Track& track, Clock::time_point now) const {
    if (track.target == nullptr) {
        return nullptr;
    }
    if (track.transition == nullptr || ProgressSince(track.startTime, now) >= 1.0) {
        return track.target->Clone();
    }
    return track.transition->Sample(ProgressSince(track.startTime, now));
}
