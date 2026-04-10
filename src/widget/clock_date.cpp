#include "clock_date.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

const char* ClockDateWidget::TypeName() const {
    return "clock_date";
}

void ClockDateWidget::Initialize(const LayoutNodeConfig&) {}

int ClockDateWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.FontMetrics().clockDate;
}

bool ClockDateWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockDateWidget::Draw(
    DashboardRenderer& renderer, HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    renderer.DrawTextBlock(hdc,
        widget.rect,
        metrics.ResolveClockDate(),
        renderer.WidgetFonts().clockDate,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontClockDate, 0,
            renderer.Config().layout.fonts.clockDate.size));
}
