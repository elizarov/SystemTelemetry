#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "renderer/render_types.h"

enum class AnimationDataKind {
    ScalarFill,
    ThroughputChart,
};

struct AnimationDataKey {
    AnimationDataKind kind = AnimationDataKind::ScalarFill;
    std::string subject;
    std::string lane;

    bool operator==(const AnimationDataKey& other) const;
};

struct AnimationDataKeyHash {
    size_t operator()(const AnimationDataKey& key) const;
};

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

enum class AnimationPrimitiveKind {
    PillBar,
    Gauge,
    ThroughputChart,
    StackedActivity,
};

enum class AnimationCompositionPlane {
    AboveSnapshot,
    AboveOverlay,
};

struct PillBarAnimationPayload {
    int markerMinWidth = 1;
    RenderColorId fillColor = RenderColorId::Accent;
    RenderColorId peakColor = RenderColorId::PeakGhost;
};

struct GaugeAnimationPayload {
    std::vector<RenderArc> ringSegments;
    std::vector<RenderRect> ringSegmentBounds;
    int ringThickness = 1;
    RenderColorId fillColor = RenderColorId::Accent;
    RenderColorId peakColor = RenderColorId::PeakGhost;
};

struct ThroughputChartAnimationPayload {
    int graphLeft = 0;
    int graphRight = 0;
    int graphBottom = 0;
    int plotTop = 0;
    int plotHeight = 1;
    int plotWidth = 1;
    int plotStrokeWidth = 1;
    int guideStrokeWidth = 1;
    int leaderDiameter = 1;
    double timeMarkerIntervalSamples = 20.0;
};

struct StackedActivityAnimationPayload {
    int segmentCount = 1;
    int segmentGap = 0;
    RenderColorId fillColor = RenderColorId::Accent;
};

struct DashboardAnimationPrimitive {
    AnimationPrimitiveKind kind = AnimationPrimitiveKind::PillBar;
    AnimationCompositionPlane plane = AnimationCompositionPlane::AboveSnapshot;
    AnimationDataKey dataKey;
    RenderRect bounds{};
    PillBarAnimationPayload pillBar{};
    GaugeAnimationPayload gauge{};
    ThroughputChartAnimationPayload throughputChart{};
    StackedActivityAnimationPayload stackedActivity{};
};

struct DashboardAnimationScene {
    std::vector<DashboardAnimationPrimitive> primitives;
};
