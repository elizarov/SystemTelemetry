#include "clock_date.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

DashboardWidgetClass ClockDateWidget::Class() const {
    return DashboardWidgetClass::ClockDate;
}

std::unique_ptr<DashboardWidget> ClockDateWidget::Clone() const {
    return std::make_unique<ClockDateWidget>(*this);
}

void ClockDateWidget::Initialize(const LayoutNodeConfig&) {}

int ClockDateWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.FontMetrics().clockDate;
}

bool ClockDateWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockDateWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveClockDate();
    renderer.DrawTextBlock(hdc,
        widget.rect,
        text,
        renderer.WidgetFonts().clockDate,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    renderer.RegisterDynamicTextAnchor(widget.rect,
        text,
        renderer.WidgetFonts().clockDate,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontClockDate,
            0,
            renderer.Config().layout.fonts.clockDate.size));
}
