#include "widget/impl/animation_primitives.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "telemetry/timing.h"
#include "util/numeric_safety.h"

namespace {

constexpr double kEpsilon = 0.000001;

const void* ScalarFillAnimationTypeToken() {
    static const int token = 0;
    return &token;
}

const void* ThroughputChartAnimationTypeToken() {
    static const int token = 0;
    return &token;
}

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

ScalarFillSample SampleScalar(const ScalarFillSample& start, const ScalarFillSample& target, double progress) {
    ScalarFillSample sample;
    if (target.valueRatio.has_value()) {
        sample.valueRatio = std::clamp(Lerp(start.valueRatio.value_or(0.0), *target.valueRatio, progress), 0.0, 1.0);
    } else if (start.valueRatio.has_value() && progress < 1.0) {
        sample.valueRatio = std::clamp(Lerp(*start.valueRatio, 0.0, progress), 0.0, 1.0);
    }

    if (target.peakRatio.has_value()) {
        const double startPeak = start.peakRatio.value_or(*target.peakRatio);
        sample.peakRatio = std::clamp(Lerp(startPeak, *target.peakRatio, progress), 0.0, 1.0);
    } else if (start.peakRatio.has_value() && progress < 1.0) {
        sample.peakRatio = std::clamp(Lerp(*start.peakRatio, 0.0, progress), 0.0, 1.0);
    }
    return sample;
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
    sample.liveLeaderMbps = FiniteNonNegativeOr(sample.liveLeaderMbps);
    sample.maxGraph = IsFiniteDouble(sample.maxGraph) && sample.maxGraph > 0.0 ? sample.maxGraph : 10.0;
    sample.timeMarkerOffsetSamples = FiniteNonNegativeOr(sample.timeMarkerOffsetSamples);
    sample.plotShiftSamples = FiniteNonNegativeOr(sample.plotShiftSamples);
    sample.guideStepMbps =
        IsFiniteDouble(sample.guideStepMbps) && sample.guideStepMbps > 0.0 ? sample.guideStepMbps : 5.0;
    if (sample.bodySampleCount == 0) {
        sample.bodySampleCount = sample.samples.size();
    }
    return sample;
}

bool DoublesEqual(double left, double right) {
    return std::abs(left - right) <= kEpsilon;
}

bool ThroughputEqual(const ThroughputChartSample& left, const ThroughputChartSample& right) {
    if (!DoublesEqual(left.maxGraph, right.maxGraph) || !DoublesEqual(left.liveLeaderMbps, right.liveLeaderMbps) ||
        !DoublesEqual(left.timeMarkerOffsetSamples, right.timeMarkerOffsetSamples) ||
        !DoublesEqual(left.plotShiftSamples, right.plotShiftSamples) ||
        !DoublesEqual(left.guideStepMbps, right.guideStepMbps) || left.bodySampleCount != right.bodySampleCount ||
        left.samples.size() != right.samples.size()) {
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
    start.liveLeaderMbps = 0.0;
    start.maxGraph = 0.0;
    start.timeMarkerOffsetSamples = target.timeMarkerOffsetSamples;
    start.plotShiftSamples = 0.0;
    start.guideStepMbps = target.guideStepMbps;
    start.bodySampleCount = target.bodySampleCount;
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
    if (!DoublesEqual(start.maxGraph, target.maxGraph) || !DoublesEqual(start.liveLeaderMbps, target.liveLeaderMbps) ||
        !DoublesEqual(start.timeMarkerOffsetSamples, target.timeMarkerOffsetSamples) ||
        !DoublesEqual(start.plotShiftSamples, target.plotShiftSamples) ||
        start.bodySampleCount != target.bodySampleCount) {
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
        markerDelta += kThroughputTimeMarkerIntervalSamples;
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
    if (targetSampleCount < 2 || overlap == 0) {
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
    if (overlap < targetSampleCount) {
        for (size_t index = overlap; index < target.samples.size(); ++index) {
            scrollSamples.push_back(target.samples[index]);
        }
    }
    targetPlotShiftSamples = static_cast<double>(scrollSamples.size() - targetSampleCount) + target.plotShiftSamples;
    if (targetPlotShiftSamples <= start.plotShiftSamples + kEpsilon) {
        scrollSamples.clear();
        targetPlotShiftSamples = 0.0;
    }
}

class ScalarFillAnimationTransition final : public WidgetAnimationTransition {
public:
    ScalarFillAnimationTransition(ScalarFillSample start, ScalarFillSample target) :
        start_(std::move(start)),
        target_(std::move(target)) {}

    WidgetAnimationStatePtr Sample(double progress) const override {
        if (progress >= 1.0) {
            return MakeScalarFillAnimationState(target_);
        }
        return MakeScalarFillAnimationState(SampleScalar(start_, target_, std::clamp(progress, 0.0, 1.0)));
    }

    bool HasActiveChange() const override {
        return ScalarHasActiveChange(start_, target_);
    }

private:
    ScalarFillSample start_;
    ScalarFillSample target_;
};

class ThroughputChartAnimationTransition final : public WidgetAnimationTransition {
public:
    ThroughputChartAnimationTransition(ThroughputChartSample start, ThroughputChartSample target) :
        start_(std::move(start)),
        target_(std::move(target)) {
        ConfigureThroughputScroll(scrollSamples_, targetPlotShiftSamples_, start_, target_);
    }

    WidgetAnimationStatePtr Sample(double progress) const override {
        progress = std::clamp(progress, 0.0, 1.0);
        if (progress >= 1.0) {
            return MakeThroughputChartAnimationState(target_);
        }

        const size_t outputCount = (std::max)(start_.samples.size(), target_.samples.size());
        ThroughputChartSample sample;
        if (!scrollSamples_.empty()) {
            sample.plotShiftSamples = Lerp(start_.plotShiftSamples, targetPlotShiftSamples_, progress);
            sample.samples = ScrollSamplesForShift(scrollSamples_, target_.samples.size(), sample.plotShiftSamples);
        } else {
            sample.samples.reserve(outputCount);
            for (size_t index = 0; index < outputCount; ++index) {
                sample.samples.push_back(Lerp(AlignedSampleValue(start_.samples, index, outputCount),
                    AlignedSampleValue(target_.samples, index, outputCount),
                    progress));
            }
            sample.plotShiftSamples = Lerp(start_.plotShiftSamples, target_.plotShiftSamples, progress);
        }

        sample.liveLeaderMbps = Lerp(start_.liveLeaderMbps, target_.liveLeaderMbps, progress);
        sample.maxGraph = Lerp(start_.maxGraph, target_.maxGraph, progress);
        const double markerDelta = ForwardMarkerDelta(start_.timeMarkerOffsetSamples, target_.timeMarkerOffsetSamples);
        sample.timeMarkerOffsetSamples = start_.timeMarkerOffsetSamples + (markerDelta * progress);
        sample.guideStepMbps = target_.guideStepMbps;
        sample.bodySampleCount = target_.bodySampleCount;
        return MakeThroughputChartAnimationState(std::move(sample));
    }

    bool HasActiveChange() const override {
        return ThroughputHasActiveChange(start_, target_);
    }

private:
    ThroughputChartSample start_;
    ThroughputChartSample target_;
    std::vector<double> scrollSamples_;
    double targetPlotShiftSamples_ = 0.0;
};

}  // namespace

ScalarFillAnimationState::ScalarFillAnimationState(ScalarFillSample sample) :
    sample_(SanitizeScalar(std::move(sample))) {}

const ScalarFillSample& ScalarFillAnimationState::Sample() const {
    return sample_;
}

const void* ScalarFillAnimationState::TypeToken() const {
    return ScalarFillAnimationTypeToken();
}

WidgetAnimationStatePtr ScalarFillAnimationState::Clone() const {
    return std::make_unique<ScalarFillAnimationState>(sample_);
}

bool ScalarFillAnimationState::Equals(const WidgetAnimationState& other) const {
    if (other.TypeToken() != TypeToken()) {
        return false;
    }
    return ScalarEqual(sample_, ScalarFillSampleFromState(other));
}

WidgetAnimationStatePtr ScalarFillAnimationState::InitialState() const {
    return MakeScalarFillAnimationState(InitialScalarStart(sample_));
}

WidgetAnimationStatePtr ScalarFillAnimationState::RetargetStart(const WidgetAnimationState& sampled) const {
    return MakeScalarFillAnimationState(RetargetScalarStart(ScalarFillSampleFromState(sampled), sample_));
}

WidgetAnimationTransitionPtr ScalarFillAnimationState::TransitionFrom(const WidgetAnimationState& start) const {
    return std::make_unique<ScalarFillAnimationTransition>(ScalarFillSampleFromState(start), sample_);
}

WidgetAnimationStatePtr MakeScalarFillAnimationState(ScalarFillSample sample) {
    return std::make_unique<ScalarFillAnimationState>(std::move(sample));
}

const ScalarFillSample& ScalarFillSampleFromState(const WidgetAnimationState& state) {
    return static_cast<const ScalarFillAnimationState&>(state).Sample();
}

ThroughputChartAnimationState::ThroughputChartAnimationState(ThroughputChartSample sample) :
    sample_(SanitizeThroughput(std::move(sample))) {}

const ThroughputChartSample& ThroughputChartAnimationState::Sample() const {
    return sample_;
}

const void* ThroughputChartAnimationState::TypeToken() const {
    return ThroughputChartAnimationTypeToken();
}

WidgetAnimationStatePtr ThroughputChartAnimationState::Clone() const {
    return std::make_unique<ThroughputChartAnimationState>(sample_);
}

bool ThroughputChartAnimationState::Equals(const WidgetAnimationState& other) const {
    if (other.TypeToken() != TypeToken()) {
        return false;
    }
    return ThroughputEqual(sample_, ThroughputChartSampleFromState(other));
}

WidgetAnimationStatePtr ThroughputChartAnimationState::InitialState() const {
    return MakeThroughputChartAnimationState(InitialThroughputStart(sample_));
}

WidgetAnimationStatePtr ThroughputChartAnimationState::RetargetStart(const WidgetAnimationState& sampled) const {
    return sampled.Clone();
}

WidgetAnimationTransitionPtr ThroughputChartAnimationState::TransitionFrom(const WidgetAnimationState& start) const {
    return std::make_unique<ThroughputChartAnimationTransition>(ThroughputChartSampleFromState(start), sample_);
}

WidgetAnimationStatePtr MakeThroughputChartAnimationState(ThroughputChartSample sample) {
    return std::make_unique<ThroughputChartAnimationState>(std::move(sample));
}

const ThroughputChartSample& ThroughputChartSampleFromState(const WidgetAnimationState& state) {
    return static_cast<const ThroughputChartAnimationState&>(state).Sample();
}
