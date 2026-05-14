#include "dashboard_renderer/impl/animation_timeline.h"

#include <algorithm>
#include <cmath>

#include "util/numeric_safety.h"

namespace {

constexpr double kEpsilon = 0.000001;
constexpr double kTimeMarkerIntervalSamples = 20.0;

std::optional<double> ClampRatio(std::optional<double> value) {
    if (!value.has_value() || !IsFiniteDouble(*value)) {
        return std::nullopt;
    }
    return std::clamp(*value, 0.0, 1.0);
}

ScalarFillSample SanitizeScalar(ScalarFillSample sample) {
    sample.valueRatio = ClampRatio(sample.valueRatio);
    sample.peakRatio = ClampRatio(sample.peakRatio);
    return sample;
}

bool OptionalDoubleEqual(std::optional<double> left, std::optional<double> right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }
    return std::abs(*left - *right) <= kEpsilon;
}

bool ScalarEqual(const ScalarFillSample& left, const ScalarFillSample& right) {
    return OptionalDoubleEqual(left.valueRatio, right.valueRatio) &&
           OptionalDoubleEqual(left.peakRatio, right.peakRatio);
}

double Lerp(double start, double target, double progress) {
    return start + ((target - start) * progress);
}

ScalarFillSample InitialScalarStart(const ScalarFillSample& target) {
    ScalarFillSample start;
    if (target.valueRatio.has_value()) {
        start.valueRatio = 0.0;
    }
    if (target.peakRatio.has_value()) {
        start.peakRatio = target.peakRatio;
    }
    return start;
}

ScalarFillSample RetargetScalarStart(ScalarFillSample sampled, const ScalarFillSample& target) {
    if (!sampled.valueRatio.has_value() && target.valueRatio.has_value()) {
        sampled.valueRatio = 0.0;
    }
    if (!sampled.peakRatio.has_value() && target.peakRatio.has_value()) {
        sampled.peakRatio = target.peakRatio;
    }
    return sampled;
}

bool ScalarHasActiveChange(const ScalarFillSample& start, const ScalarFillSample& target) {
    if (start.valueRatio.has_value() || target.valueRatio.has_value()) {
        if (!OptionalDoubleEqual(start.valueRatio.value_or(0.0), target.valueRatio.value_or(0.0))) {
            return true;
        }
        if (start.valueRatio.has_value() != target.valueRatio.has_value()) {
            return true;
        }
    }
    if (target.peakRatio.has_value()) {
        return start.peakRatio.has_value() && !OptionalDoubleEqual(start.peakRatio, target.peakRatio);
    }
    return start.peakRatio.has_value();
}

std::vector<double> SanitizeSamples(const std::vector<double>& samples) {
    std::vector<double> sanitized;
    sanitized.reserve(samples.size());
    for (double sample : samples) {
        sanitized.push_back(FiniteNonNegativeOr(sample));
    }
    return sanitized;
}

ThroughputChartSample SanitizeThroughput(ThroughputChartSample sample) {
    sample.samples = SanitizeSamples(sample.samples);
    sample.maxGraph = IsFiniteDouble(sample.maxGraph) && sample.maxGraph > 0.0 ? sample.maxGraph : 10.0;
    sample.timeMarkerOffsetSamples = FiniteNonNegativeOr(sample.timeMarkerOffsetSamples);
    sample.plotShiftSamples = FiniteNonNegativeOr(sample.plotShiftSamples);
    sample.guideStepMbps =
        IsFiniteDouble(sample.guideStepMbps) && sample.guideStepMbps > 0.0 ? sample.guideStepMbps : 5.0;
    return sample;
}

bool DoublesEqual(double left, double right) {
    return std::abs(left - right) <= kEpsilon;
}

bool ThroughputEqual(const ThroughputChartSample& left, const ThroughputChartSample& right) {
    if (!DoublesEqual(left.maxGraph, right.maxGraph) ||
        !DoublesEqual(left.timeMarkerOffsetSamples, right.timeMarkerOffsetSamples) ||
        !DoublesEqual(left.plotShiftSamples, right.plotShiftSamples) ||
        !DoublesEqual(left.guideStepMbps, right.guideStepMbps) || left.samples.size() != right.samples.size()) {
        return false;
    }
    for (size_t index = 0; index < left.samples.size(); ++index) {
        if (!DoublesEqual(left.samples[index], right.samples[index])) {
            return false;
        }
    }
    return true;
}

ThroughputChartSample InitialThroughputStart(const ThroughputChartSample& target) {
    ThroughputChartSample start;
    start.samples.assign(target.samples.size(), 0.0);
    start.maxGraph = 0.0;
    start.timeMarkerOffsetSamples = target.timeMarkerOffsetSamples;
    start.plotShiftSamples = 0.0;
    start.guideStepMbps = target.guideStepMbps;
    return start;
}

double AlignedSampleValue(const std::vector<double>& samples, size_t outputIndex, size_t outputCount) {
    if (samples.empty() || outputIndex >= outputCount) {
        return 0.0;
    }
    const size_t leadingMissing = outputCount > samples.size() ? outputCount - samples.size() : 0;
    if (outputIndex < leadingMissing) {
        return 0.0;
    }
    const size_t sampleIndex = outputIndex - leadingMissing;
    return sampleIndex < samples.size() ? samples[sampleIndex] : 0.0;
}

bool ThroughputHasActiveChange(const ThroughputChartSample& start, const ThroughputChartSample& target) {
    if (!DoublesEqual(start.maxGraph, target.maxGraph) ||
        !DoublesEqual(start.timeMarkerOffsetSamples, target.timeMarkerOffsetSamples) ||
        !DoublesEqual(start.plotShiftSamples, target.plotShiftSamples)) {
        return true;
    }
    const size_t outputCount = (std::max)(start.samples.size(), target.samples.size());
    for (size_t index = 0; index < outputCount; ++index) {
        if (!DoublesEqual(AlignedSampleValue(start.samples, index, outputCount),
                AlignedSampleValue(target.samples, index, outputCount))) {
            return true;
        }
    }
    return false;
}

double ForwardMarkerDelta(double startOffsetSamples, double targetOffsetSamples) {
    double markerDelta = targetOffsetSamples - startOffsetSamples;
    if (markerDelta < 0.0) {
        markerDelta += kTimeMarkerIntervalSamples;
    }
    return markerDelta;
}

size_t PlotTailCount(double plotShiftSamples, size_t maxTailCount) {
    if (plotShiftSamples <= kEpsilon || maxTailCount == 0) {
        return 0;
    }
    return (std::min)(static_cast<size_t>(std::ceil(plotShiftSamples - kEpsilon)), maxTailCount);
}

bool SamplesEqualAt(
    const std::vector<double>& left, size_t leftIndex, const std::vector<double>& right, size_t rightIndex) {
    return leftIndex < left.size() && rightIndex < right.size() && DoublesEqual(left[leftIndex], right[rightIndex]);
}

size_t SuffixPrefixOverlap(const std::vector<double>& samples, const std::vector<double>& targetSamples) {
    const size_t maxOverlap = (std::min)(samples.size(), targetSamples.size());
    for (size_t overlap = maxOverlap; overlap > 0; --overlap) {
        const size_t sampleStart = samples.size() - overlap;
        bool allEqual = true;
        for (size_t index = 0; index < overlap; ++index) {
            if (!SamplesEqualAt(samples, sampleStart + index, targetSamples, index)) {
                allEqual = false;
                break;
            }
        }
        if (allEqual) {
            return overlap;
        }
    }
    return 0;
}

bool OverlapSupportsScroll(size_t overlap, size_t targetSampleCount) {
    if (targetSampleCount < 2 || overlap >= targetSampleCount) {
        return false;
    }
    return overlap + 1u >= targetSampleCount;
}

std::vector<double> ScrollSamplesForShift(
    const std::vector<double>& scrollSamples, size_t visibleSampleCount, double plotShiftSamples) {
    const size_t tailCapacity =
        scrollSamples.size() > visibleSampleCount ? scrollSamples.size() - visibleSampleCount : 0;
    const size_t sampleCount =
        (std::min)(scrollSamples.size(), visibleSampleCount + PlotTailCount(plotShiftSamples, tailCapacity));
    std::vector<double> samples;
    samples.reserve(sampleCount);
    for (size_t index = 0; index < sampleCount; ++index) {
        samples.push_back(scrollSamples[index]);
    }
    return samples;
}

void ConfigureThroughputScroll(std::vector<double>& scrollSamples,
    double& targetPlotShiftSamples,
    const ThroughputChartSample& start,
    const ThroughputChartSample& target) {
    scrollSamples.clear();
    targetPlotShiftSamples = 0.0;
    const size_t targetSampleCount = target.samples.size();
    const size_t overlap = SuffixPrefixOverlap(start.samples, target.samples);
    if (!OverlapSupportsScroll(overlap, targetSampleCount)) {
        return;
    }

    scrollSamples = start.samples;
    for (size_t index = overlap; index < target.samples.size(); ++index) {
        scrollSamples.push_back(target.samples[index]);
    }
    targetPlotShiftSamples = static_cast<double>(scrollSamples.size() - targetSampleCount);
    if (targetPlotShiftSamples <= start.plotShiftSamples + kEpsilon) {
        scrollSamples.clear();
        targetPlotShiftSamples = 0.0;
    }
}

}  // namespace

DashboardAnimationTimeline::DashboardAnimationTimeline(std::chrono::milliseconds duration)
    : duration_(duration.count() > 0 ? duration : std::chrono::milliseconds(1)) {}

void DashboardAnimationTimeline::BeginFrame(Clock::time_point now) {
    frameTime_ = now;
    frameActive_ = true;
    for (auto& entry : scalarTracks_) {
        entry.second.touched = false;
    }
    for (auto& entry : throughputTracks_) {
        entry.second.touched = false;
    }
}

ScalarFillSample DashboardAnimationTimeline::ResolveScalar(
    const AnimationDataKey& key, const ScalarFillSample& target) {
    const ScalarFillSample sanitizedTarget = SanitizeScalar(target);
    if (!frameActive_) {
        return sanitizedTarget;
    }

    auto it = scalarTracks_.find(key);
    if (it == scalarTracks_.end()) {
        ScalarTrack track;
        track.start = InitialScalarStart(sanitizedTarget);
        track.target = sanitizedTarget;
        track.startTime = frameTime_;
        track.touched = true;
        it = scalarTracks_.insert({key, std::move(track)}).first;
    } else {
        ScalarTrack& track = it->second;
        if (!ScalarEqual(track.target, sanitizedTarget)) {
            track.start = RetargetScalarStart(SampleScalar(track, frameTime_), sanitizedTarget);
            track.target = sanitizedTarget;
            track.startTime = frameTime_;
        }
        track.touched = true;
    }

    return SampleScalar(it->second, frameTime_);
}

ThroughputChartSample DashboardAnimationTimeline::ResolveThroughput(
    const AnimationDataKey& key, const ThroughputChartSample& target) {
    const ThroughputChartSample sanitizedTarget = SanitizeThroughput(target);
    if (!frameActive_) {
        return sanitizedTarget;
    }

    auto it = throughputTracks_.find(key);
    if (it == throughputTracks_.end()) {
        ThroughputTrack track;
        track.start = InitialThroughputStart(sanitizedTarget);
        track.target = sanitizedTarget;
        track.startTime = frameTime_;
        track.touched = true;
        it = throughputTracks_.insert({key, std::move(track)}).first;
    } else {
        ThroughputTrack& track = it->second;
        if (!ThroughputEqual(track.target, sanitizedTarget)) {
            track.start = SampleThroughput(track, frameTime_);
            track.target = sanitizedTarget;
            ConfigureThroughputScroll(track.scrollSamples, track.targetPlotShiftSamples, track.start, track.target);
            track.startTime = frameTime_;
        }
        track.touched = true;
    }

    return SampleThroughput(it->second, frameTime_);
}

void DashboardAnimationTimeline::EndFrame() {
    for (auto it = scalarTracks_.begin(); it != scalarTracks_.end();) {
        if (!it->second.touched) {
            it = scalarTracks_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = throughputTracks_.begin(); it != throughputTracks_.end();) {
        if (!it->second.touched) {
            it = throughputTracks_.erase(it);
        } else {
            ++it;
        }
    }
    frameActive_ = false;
}

void DashboardAnimationTimeline::Reset() {
    scalarTracks_.clear();
    throughputTracks_.clear();
    frameActive_ = false;
}

bool DashboardAnimationTimeline::HasActiveAnimations(Clock::time_point now) const {
    for (const auto& entry : scalarTracks_) {
        const ScalarTrack& track = entry.second;
        if (ScalarHasActiveChange(track.start, track.target) && ProgressSince(track.startTime, now) < 1.0) {
            return true;
        }
    }
    for (const auto& entry : throughputTracks_) {
        const ThroughputTrack& track = entry.second;
        if (ThroughputHasActiveChange(track.start, track.target) && ProgressSince(track.startTime, now) < 1.0) {
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

ScalarFillSample DashboardAnimationTimeline::SampleScalar(const ScalarTrack& track, Clock::time_point now) const {
    const double progress = ProgressSince(track.startTime, now);
    ScalarFillSample sample;
    if (track.target.valueRatio.has_value()) {
        sample.valueRatio =
            std::clamp(Lerp(track.start.valueRatio.value_or(0.0), *track.target.valueRatio, progress), 0.0, 1.0);
    } else if (track.start.valueRatio.has_value() && progress < 1.0) {
        sample.valueRatio = std::clamp(Lerp(*track.start.valueRatio, 0.0, progress), 0.0, 1.0);
    }

    if (track.target.peakRatio.has_value()) {
        const double startPeak = track.start.peakRatio.value_or(*track.target.peakRatio);
        sample.peakRatio = std::clamp(Lerp(startPeak, *track.target.peakRatio, progress), 0.0, 1.0);
    } else if (track.start.peakRatio.has_value() && progress < 1.0) {
        sample.peakRatio = std::clamp(Lerp(*track.start.peakRatio, 0.0, progress), 0.0, 1.0);
    }
    return sample;
}

ThroughputChartSample DashboardAnimationTimeline::SampleThroughput(
    const ThroughputTrack& track, Clock::time_point now) const {
    const double progress = ProgressSince(track.startTime, now);
    if (progress >= 1.0) {
        ThroughputChartSample sample = track.target;
        sample.plotShiftSamples = 0.0;
        return sample;
    }

    const size_t outputCount = (std::max)(track.start.samples.size(), track.target.samples.size());

    ThroughputChartSample sample;
    if (!track.scrollSamples.empty()) {
        sample.plotShiftSamples = Lerp(track.start.plotShiftSamples, track.targetPlotShiftSamples, progress);
        sample.samples =
            ScrollSamplesForShift(track.scrollSamples, track.target.samples.size(), sample.plotShiftSamples);
    } else {
        sample.samples.reserve(outputCount);
        for (size_t index = 0; index < outputCount; ++index) {
            sample.samples.push_back(Lerp(AlignedSampleValue(track.start.samples, index, outputCount),
                AlignedSampleValue(track.target.samples, index, outputCount),
                progress));
        }
        sample.plotShiftSamples = 0.0;
    }

    sample.maxGraph = Lerp(track.start.maxGraph, track.target.maxGraph, progress);
    const double markerDelta =
        ForwardMarkerDelta(track.start.timeMarkerOffsetSamples, track.target.timeMarkerOffsetSamples);
    sample.timeMarkerOffsetSamples = track.start.timeMarkerOffsetSamples + (markerDelta * progress);
    sample.guideStepMbps = track.target.guideStepMbps;
    return sample;
}
