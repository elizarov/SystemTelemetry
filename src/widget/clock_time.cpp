#include "clock_time.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

const char* ClockTimeWidget::TypeName() const {
    return "clock_time";
}

void ClockTimeWidget::Initialize(const LayoutNodeConfig&) {}

int ClockTimeWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.FontMetrics().clockTime;
}

bool ClockTimeWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockTimeWidget::Draw(
    DashboardRenderer& renderer, HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    renderer.DrawTextBlock(hdc,
        widget.rect,
        metrics.ResolveClockTime(),
        renderer.WidgetFonts().clockTime,
        renderer.ForegroundColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontClockTime, 0,
            renderer.Config().layout.fonts.clockTime.size));
}
