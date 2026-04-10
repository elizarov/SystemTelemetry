#include "clock_time.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

DashboardWidgetClass ClockTimeWidget::Class() const {
    return DashboardWidgetClass::ClockTime;
}

std::unique_ptr<DashboardWidget> ClockTimeWidget::Clone() const {
    return std::make_unique<ClockTimeWidget>(*this);
}

void ClockTimeWidget::Initialize(const LayoutNodeConfig&) {}

int ClockTimeWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.FontMetrics().clockTime;
}

bool ClockTimeWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockTimeWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    renderer.DrawTextBlock(hdc,
        widget.rect,
        metrics.ResolveClockTime(),
        renderer.WidgetFonts().clockTime,
        renderer.ForegroundColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::AnchorEditParameter::FontClockTime,
            0,
            renderer.Config().layout.fonts.clockTime.size));
}
