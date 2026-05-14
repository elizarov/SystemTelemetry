#include "widget/widget_host.h"

ScalarFillSample WidgetHost::ResolveAnimatedScalarFill(
    const AnimationDataKey&, const ScalarFillSample& target, AnimationCompositionPlane) {
    return target;
}

ThroughputChartSample WidgetHost::ResolveAnimatedThroughputChart(
    const AnimationDataKey&, const ThroughputChartSample& target, AnimationCompositionPlane) {
    return target;
}

void WidgetHost::RegisterAnimationPrimitive(const DashboardAnimationPrimitive&) {}
