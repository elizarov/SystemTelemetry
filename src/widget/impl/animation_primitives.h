#pragma once

#include <optional>
#include <vector>

#include "widget/animation.h"

// Timeline samples stay geometry-free so layout and scale changes can reuse stored animation tracks.
struct ScalarFillSample {
    std::optional<double> valueRatio;
    std::optional<double> peakRatio;
};

struct ThroughputChartSample {
    std::vector<double> samples;
    double maxGraph = 10.0;
    double timeMarkerOffsetSamples = 0.0;
    double plotShiftSamples = 0.0;
    double guideStepMbps = 5.0;
};

class ScalarFillAnimationState final : public WidgetAnimationState {
public:
    explicit ScalarFillAnimationState(ScalarFillSample sample);

    const ScalarFillSample& Sample() const;
    const void* TypeToken() const override;
    WidgetAnimationStatePtr Clone() const override;
    bool Equals(const WidgetAnimationState& other) const override;
    WidgetAnimationStatePtr InitialState() const override;
    WidgetAnimationStatePtr RetargetStart(const WidgetAnimationState& sampled) const override;
    WidgetAnimationTransitionPtr TransitionFrom(const WidgetAnimationState& start) const override;

private:
    ScalarFillSample sample_;
};

WidgetAnimationStatePtr MakeScalarFillAnimationState(ScalarFillSample sample);
const ScalarFillSample& ScalarFillSampleFromState(const WidgetAnimationState& state);

class ThroughputChartAnimationState final : public WidgetAnimationState {
public:
    explicit ThroughputChartAnimationState(ThroughputChartSample sample);

    const ThroughputChartSample& Sample() const;
    const void* TypeToken() const override;
    WidgetAnimationStatePtr Clone() const override;
    bool Equals(const WidgetAnimationState& other) const override;
    WidgetAnimationStatePtr InitialState() const override;
    WidgetAnimationStatePtr RetargetStart(const WidgetAnimationState& sampled) const override;
    WidgetAnimationTransitionPtr TransitionFrom(const WidgetAnimationState& start) const override;

private:
    ThroughputChartSample sample_;
};

WidgetAnimationStatePtr MakeThroughputChartAnimationState(ThroughputChartSample sample);
const ThroughputChartSample& ThroughputChartSampleFromState(const WidgetAnimationState& state);
