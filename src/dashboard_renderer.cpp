#include "dashboard_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <objidl.h>
#include <optional>
#include <sstream>
#include <set>
#include <vector>

#include <gdiplus.h>

#include "utf8.h"

namespace {

struct GaugeSegmentLayout {
    int segmentCount = 1;
    double totalSweep = 0.0;
    double gapSweep = 360.0;
    double segmentGap = 0.0;
    double segmentSweep = 0.0;
    double pitchSweep = 0.0;
    double gaugeStart = 90.0;
    double gaugeEnd = 90.0;
};

GaugeSegmentLayout ComputeGaugeSegmentLayout(double requestedSweep, int requestedSegmentCount, double requestedSegmentGap) {
    GaugeSegmentLayout layout;
    layout.segmentCount = std::max(1, requestedSegmentCount);
    layout.totalSweep = std::clamp(requestedSweep, 0.0, 360.0);
    layout.gapSweep = std::max(0.0, 360.0 - layout.totalSweep);
    layout.gaugeStart = 90.0 + (layout.gapSweep / 2.0);
    layout.gaugeEnd = layout.gaugeStart + layout.totalSweep;

    if (layout.segmentCount <= 1) {
        layout.segmentGap = 0.0;
        layout.segmentSweep = layout.totalSweep;
        layout.pitchSweep = layout.totalSweep;
        return layout;
    }

    const double maxSegmentGap = layout.totalSweep / static_cast<double>(layout.segmentCount - 1);
    layout.segmentGap = std::clamp(requestedSegmentGap, 0.0, maxSegmentGap);
    layout.segmentSweep = std::max(0.0,
        (layout.totalSweep - (layout.segmentGap * static_cast<double>(layout.segmentCount - 1))) /
            static_cast<double>(layout.segmentCount));
    layout.pitchSweep = layout.segmentSweep + layout.segmentGap;
    return layout;
}

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
}

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

std::string FormatWin32Error(DWORD error) {
    if (error == 0) {
        return "win32=0";
    }
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string message = "win32=" + std::to_string(error);
    if (length != 0 && buffer != nullptr) {
        message += " ";
        message += Utf8FromWide(std::wstring(buffer, length));
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        const std::string trimmed = Trim(item);
        if (!trimmed.empty()) {
            parts.push_back(trimmed);
        }
    }
    return parts;
}

DashboardMetricListEntry ParseMetricListEntry(std::string item) {
    DashboardMetricListEntry entry;
    const size_t equals = item.find('=');
    if (equals == std::string::npos) {
        entry.metricRef = Trim(item);
        return entry;
    }

    entry.metricRef = Trim(item.substr(0, equals));
    entry.labelOverride = Trim(item.substr(equals + 1));
    return entry;
}

std::vector<DashboardMetricListEntry> ParseMetricListEntries(const std::string& parameter) {
    std::vector<DashboardMetricListEntry> entries;
    for (const auto& item : Split(parameter, ',')) {
        DashboardMetricListEntry entry = ParseMetricListEntry(item);
        if (!entry.metricRef.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

Gdiplus::PointF GaugePoint(float cx, float cy, float radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return Gdiplus::PointF(
        cx + static_cast<Gdiplus::REAL>(std::cos(radians) * radius),
        cy + static_cast<Gdiplus::REAL>(std::sin(radians) * radius));
}

void AddCapsulePath(Gdiplus::GraphicsPath& path, const RECT& rect) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    const int diameter = std::max(1, std::min(width, height));
    const int centerWidth = std::max(0, width - diameter);
    const int rightArcLeft = rect.left + centerWidth;

    path.StartFigure();
    path.AddArc(rect.left, rect.top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rightArcLeft, rect.top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rightArcLeft, rect.bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.left, rect.bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillCapsule(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath path;
    AddCapsulePath(path, rect);
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void FillCircle(HDC hdc, int centerX, int centerY, int diameter, COLORREF color, BYTE alpha) {
    const int clampedDiameter = std::max(1, diameter);
    const int radius = clampedDiameter / 2;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillEllipse(&brush,
        static_cast<INT>(centerX - radius),
        static_cast<INT>(centerY - radius),
        static_cast<INT>(clampedDiameter),
        static_cast<INT>(clampedDiameter));
}

void FillDiamond(HDC hdc, const RECT& rect, COLORREF color) {
    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    const int centerX = rect.left + (width / 2);
    const int centerY = rect.top + (height / 2);
    Gdiplus::Point points[] = {
        Gdiplus::Point(centerX, rect.top),
        Gdiplus::Point(rect.right - 1, centerY),
        Gdiplus::Point(centerX, rect.bottom - 1),
        Gdiplus::Point(rect.left, centerY),
    };

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPolygon(&brush, points, static_cast<INT>(std::size(points)));
}

void DrawSegmentIndicator(HDC hdc, const RECT& rect, int segmentCount, int segmentGap, double ratio,
    COLORREF trackColor, COLORREF accentColor) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0 || segmentCount <= 0) {
        return;
    }

    const int totalGap = std::max(0, segmentGap) * std::max(0, segmentCount - 1);
    const int availableHeight = std::max(segmentCount, height - totalGap);
    const int baseSegmentHeight = std::max(1, availableHeight / segmentCount);
    const int remainder = std::max(0, availableHeight - (baseSegmentHeight * segmentCount));
    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);
    const int filledSegments = clampedRatio > 0.0
        ? std::clamp(static_cast<int>(std::ceil(clampedRatio * static_cast<double>(segmentCount))), 1, segmentCount)
        : 0;
    int top = rect.top;
    for (int index = segmentCount - 1; index >= 0; --index) {
        const int extra = (segmentCount - 1 - index) < remainder ? 1 : 0;
        const int segmentHeight = baseSegmentHeight + extra;
        const int visualHeight = std::min(segmentHeight, std::max(2, width / 2));
        const int segmentTop = top + std::max(0, (segmentHeight - visualHeight) / 2);
        RECT segmentRect{rect.left, segmentTop, rect.right,
            std::min(rect.bottom, static_cast<LONG>(segmentTop + visualHeight))};
        HBRUSH trackBrush = CreateSolidBrush(trackColor);
        FillRect(hdc, &segmentRect, trackBrush);
        DeleteObject(trackBrush);

        if (index < filledSegments) {
            HBRUSH fillBrush = CreateSolidBrush(accentColor);
            FillRect(hdc, &segmentRect, fillBrush);
            DeleteObject(fillBrush);
        }

        top = segmentRect.bottom + std::max(0, segmentGap);
    }
}

void FillGaugeSegment(Gdiplus::Graphics& graphics, float cx, float cy, float radius, float thickness,
    double startAngleDegrees, double sweepAngleDegrees, const Gdiplus::Color& color) {
    if (radius <= 0.0f || thickness <= 0.0f || sweepAngleDegrees <= 0.0) {
        return;
    }

    const float outerRadius = radius + (thickness / 2.0f);
    const float innerRadius = std::max(0.0f, radius - (thickness / 2.0f));
    if (outerRadius <= innerRadius) {
        return;
    }

    const float outerDiameter = outerRadius * 2.0f;
    const float innerDiameter = innerRadius * 2.0f;
    const Gdiplus::RectF outerRect(cx - outerRadius, cy - outerRadius, outerDiameter, outerDiameter);
    const Gdiplus::RectF innerRect(cx - innerRadius, cy - innerRadius, innerDiameter, innerDiameter);
    const Gdiplus::PointF outerStart = GaugePoint(cx, cy, outerRadius, startAngleDegrees);
    const Gdiplus::PointF outerEnd = GaugePoint(cx, cy, outerRadius, startAngleDegrees + sweepAngleDegrees);
    const Gdiplus::PointF innerEnd = GaugePoint(cx, cy, innerRadius, startAngleDegrees + sweepAngleDegrees);
    const Gdiplus::PointF innerStart = GaugePoint(cx, cy, innerRadius, startAngleDegrees);

    Gdiplus::GraphicsPath path;
    path.StartFigure();
    path.AddArc(outerRect, static_cast<Gdiplus::REAL>(startAngleDegrees), static_cast<Gdiplus::REAL>(sweepAngleDegrees));
    path.AddLine(outerEnd, innerEnd);
    if (innerRadius > 0.0f) {
        path.AddArc(innerRect, static_cast<Gdiplus::REAL>(startAngleDegrees + sweepAngleDegrees),
            static_cast<Gdiplus::REAL>(-sweepAngleDegrees));
    } else {
        path.AddLine(innerEnd, Gdiplus::PointF(cx, cy));
    }
    path.AddLine(innerStart, outerStart);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

int GetImageEncoderClsid(const WCHAR* mimeType, CLSID* clsid) {
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok || encoderBytes == 0) {
        return -1;
    }

    std::vector<BYTE> encoderBuffer(encoderBytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoderBuffer.data());
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok) {
        return -1;
    }

    for (UINT i = 0; i < encoderCount; ++i) {
        if (wcscmp(encoders[i].MimeType, mimeType) == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace
DashboardRenderer::TextLayoutResult DashboardRenderer::MeasureTextBlock(HDC hdc, const RECT& rect,
    const std::string& text, HFONT font, UINT format) const {
    TextLayoutResult result{rect};
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return result;
    }

    HGDIOBJ oldFont = SelectObject(hdc, font);
    RECT measureRect{0, 0, std::max<LONG>(0, rect.right - rect.left), std::max<LONG>(0, rect.bottom - rect.top)};
    UINT measureFormat = format | DT_CALCRECT;
    measureFormat &= ~DT_VCENTER;
    measureFormat &= ~DT_BOTTOM;
    measureFormat &= ~DT_NOCLIP;
    DrawTextW(hdc, wideText.c_str(), -1, &measureRect, measureFormat);

    const int measuredWidth = std::max(0, static_cast<int>(measureRect.right - measureRect.left));
    const int measuredHeight = std::max(0, static_cast<int>(measureRect.bottom - measureRect.top));
    const int availableWidth = std::max(0, static_cast<int>(rect.right - rect.left));
    const int availableHeight = std::max(0, static_cast<int>(rect.bottom - rect.top));
    const int textWidth = std::min(availableWidth, measuredWidth);
    const int textHeight = std::min(availableHeight, measuredHeight);

    int left = rect.left;
    if ((format & DT_CENTER) != 0) {
        left = rect.left + std::max(0, (availableWidth - textWidth) / 2);
    } else if ((format & DT_RIGHT) != 0) {
        left = rect.right - textWidth;
    }

    int top = rect.top;
    if ((format & DT_VCENTER) != 0) {
        top = rect.top + std::max(0, (availableHeight - textHeight) / 2);
    } else if ((format & DT_BOTTOM) != 0) {
        top = rect.bottom - textHeight;
    }

    result.textRect = RECT{
        left,
        top,
        std::min(rect.right, static_cast<LONG>(left + textWidth)),
        std::min(rect.bottom, static_cast<LONG>(top + textHeight))
    };
    SelectObject(hdc, oldFont);
    return result;
}

DashboardRenderer::TextLayoutResult DashboardRenderer::DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text,
    HFONT font, COLORREF color, UINT format, const std::optional<EditableTextBinding>& editable) {
    TextLayoutResult result{rect};
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    const std::wstring wideText = WideFromUtf8(text);
    DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
    if (editable.has_value() && !wideText.empty()) {
        result = MeasureTextBlock(hdc, rect, text, font, format);

        const int anchorSize = std::max(4, ScaleLogical(6));
        const int anchorHalf = anchorSize / 2;
        const int anchorCenterX = result.textRect.right;
        const int anchorCenterY = result.textRect.top;
        EditableTextRegion region;
        region.key = editable->key;
        region.textRect = result.textRect;
        region.anchorRect = RECT{
            anchorCenterX - anchorHalf,
            anchorCenterY - anchorHalf,
            anchorCenterX - anchorHalf + anchorSize,
            anchorCenterY - anchorHalf + anchorSize
        };
        const int anchorHitInset = std::max(3, ScaleLogical(4));
        region.anchorHitRect = RECT{
            region.anchorRect.left - anchorHitInset,
            region.anchorRect.top - anchorHitInset,
            region.anchorRect.right + anchorHitInset,
            region.anchorRect.bottom + anchorHitInset
        };
        region.fontSize = editable->fontSize;
        editableTextRegions_.push_back(std::move(region));
    }
    return result;
}

void DashboardRenderer::DrawHoveredWidgetHighlight(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || !overlayState.hoveredEditableWidget.has_value()) {
        return;
    }

    const ResolvedWidgetLayout* hoveredWidget = nullptr;
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (MatchesWidgetIdentity(widget, *overlayState.hoveredEditableWidget)) {
                hoveredWidget = &widget;
                break;
            }
        }
        if (hoveredWidget != nullptr) {
            break;
        }
    }
    if (hoveredWidget == nullptr) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, 1, LayoutGuideColor());
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, hoveredWidget->rect.left, hoveredWidget->rect.top, hoveredWidget->rect.right, hoveredWidget->rect.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardRenderer::DrawHoveredEditableTextHighlight(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides) {
        return;
    }

    const EditableTextRegion* highlighted = nullptr;
    bool active = false;
    if (overlayState.activeEditableText.has_value()) {
        const auto it = std::find_if(editableTextRegions_.begin(), editableTextRegions_.end(),
            [&](const EditableTextRegion& region) {
                return MatchesEditableTextKey(region.key, *overlayState.activeEditableText);
            });
        if (it != editableTextRegions_.end()) {
            highlighted = &(*it);
            active = true;
        }
    }
    if (highlighted == nullptr && overlayState.hoveredEditableText.has_value()) {
        const auto it = std::find_if(editableTextRegions_.begin(), editableTextRegions_.end(),
            [&](const EditableTextRegion& region) {
                return MatchesEditableTextKey(region.key, *overlayState.hoveredEditableText);
            });
        if (it != editableTextRegions_.end()) {
            highlighted = &(*it);
        }
    }
    if (highlighted == nullptr) {
        return;
    }

    const COLORREF outlineColor = active ? ActiveEditColor() : LayoutGuideColor();
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)),
        static_cast<Gdiplus::REAL>(std::max(1, ScaleLogical(1))));
    pen.SetDashStyle(Gdiplus::DashStyleDot);
    const RECT& textRect = highlighted->textRect;
    graphics.DrawRectangle(&pen,
        static_cast<Gdiplus::REAL>(textRect.left),
        static_cast<Gdiplus::REAL>(textRect.top),
        static_cast<Gdiplus::REAL>(std::max<LONG>(1, textRect.right - textRect.left)),
        static_cast<Gdiplus::REAL>(std::max<LONG>(1, textRect.bottom - textRect.top)));

    const RECT& anchorRect = highlighted->anchorRect;
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)));
    graphics.FillEllipse(&fill,
        static_cast<Gdiplus::REAL>(anchorRect.left),
        static_cast<Gdiplus::REAL>(anchorRect.top),
        static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.right - anchorRect.left)),
        static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.bottom - anchorRect.top)));
}

void DashboardRenderer::DrawHoveredEditableAnchorHighlight(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides) {
        return;
    }

    const EditableAnchorRegion* highlighted = nullptr;
    bool active = false;
    if (overlayState.activeEditableAnchor.has_value()) {
        const auto it = std::find_if(editableAnchorRegions_.begin(), editableAnchorRegions_.end(),
            [&](const EditableAnchorRegion& region) {
                return MatchesEditableAnchorKey(region.key, *overlayState.activeEditableAnchor);
            });
        if (it != editableAnchorRegions_.end()) {
            highlighted = &(*it);
            active = true;
        }
    }
    if (highlighted == nullptr && overlayState.hoveredEditableAnchor.has_value()) {
        const auto it = std::find_if(editableAnchorRegions_.begin(), editableAnchorRegions_.end(),
            [&](const EditableAnchorRegion& region) {
                return MatchesEditableAnchorKey(region.key, *overlayState.hoveredEditableAnchor);
            });
        if (it != editableAnchorRegions_.end()) {
            highlighted = &(*it);
        }
    }
    if (highlighted == nullptr && overlayState.hoveredEditableWidget.has_value()) {
        const auto it = std::find_if(editableAnchorRegions_.begin(), editableAnchorRegions_.end(),
            [&](const EditableAnchorRegion& region) {
                return region.key.widget.renderCardId == overlayState.hoveredEditableWidget->renderCardId &&
                    region.key.widget.editCardId == overlayState.hoveredEditableWidget->editCardId &&
                    region.key.widget.nodePath == overlayState.hoveredEditableWidget->nodePath;
            });
        if (it != editableAnchorRegions_.end()) {
            highlighted = &(*it);
        }
    }
    if (highlighted == nullptr) {
        return;
    }

    const COLORREF outlineColor = active ? ActiveEditColor() : LayoutGuideColor();
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    if (highlighted->targetRect.right > highlighted->targetRect.left &&
        highlighted->targetRect.bottom > highlighted->targetRect.top) {
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)),
            static_cast<Gdiplus::REAL>(std::max(1, ScaleLogical(1))));
        pen.SetDashStyle(Gdiplus::DashStyleDot);
        const RECT& targetRect = highlighted->targetRect;
        graphics.DrawRectangle(&pen,
            static_cast<Gdiplus::REAL>(targetRect.left),
            static_cast<Gdiplus::REAL>(targetRect.top),
            static_cast<Gdiplus::REAL>(std::max<LONG>(1, targetRect.right - targetRect.left)),
            static_cast<Gdiplus::REAL>(std::max<LONG>(1, targetRect.bottom - targetRect.top)));
    }

    if (highlighted->shape == AnchorShape::Circle) {
        const RECT& anchorRect = highlighted->anchorRect;
        Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)));
        graphics.FillEllipse(&fill,
            static_cast<Gdiplus::REAL>(anchorRect.left),
            static_cast<Gdiplus::REAL>(anchorRect.top),
            static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.right - anchorRect.left)),
            static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.bottom - anchorRect.top)));
    } else {
        FillDiamond(hdc, highlighted->anchorRect, outlineColor);
    }
}

void DashboardRenderer::DrawLayoutEditGuides(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || layoutEditGuides_.empty()) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, 1, LayoutGuideColor());
    HPEN activePen = CreatePen(PS_SOLID, 1, ActiveEditColor());
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    for (const auto& guide : layoutEditGuides_) {
        const bool active = overlayState.activeLayoutEditGuide.has_value() &&
            MatchesLayoutEditGuide(guide, *overlayState.activeLayoutEditGuide);
        SelectObject(hdc, active ? activePen : pen);
        if (guide.axis == LayoutGuideAxis::Vertical) {
            MoveToEx(hdc, guide.lineRect.left, guide.lineRect.top, nullptr);
            LineTo(hdc, guide.lineRect.left, guide.lineRect.bottom);
        } else {
            MoveToEx(hdc, guide.lineRect.left, guide.lineRect.top, nullptr);
            LineTo(hdc, guide.lineRect.right, guide.lineRect.top);
        }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(activePen);
    DeleteObject(pen);
}

void DashboardRenderer::DrawWidgetEditGuides(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || widgetEditGuides_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const WidgetEditGuide& guide) {
        if (overlayState.activeWidgetEditGuide.has_value()) {
            return guide.widget.renderCardId == overlayState.activeWidgetEditGuide->widget.renderCardId &&
                guide.widget.editCardId == overlayState.activeWidgetEditGuide->widget.editCardId &&
                guide.widget.nodePath == overlayState.activeWidgetEditGuide->widget.nodePath;
        }
        if (!overlayState.hoveredEditableWidget.has_value()) {
            return false;
        }
        return guide.widget.renderCardId == overlayState.hoveredEditableWidget->renderCardId &&
            guide.widget.editCardId == overlayState.hoveredEditableWidget->editCardId &&
            guide.widget.nodePath == overlayState.hoveredEditableWidget->nodePath;
    };

    HPEN pen = CreatePen(PS_SOLID, 1, LayoutGuideColor());
    HPEN activePen = CreatePen(PS_SOLID, 1, ActiveEditColor());
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    for (const auto& guide : widgetEditGuides_) {
        if (!shouldDraw(guide)) {
            continue;
        }
        const bool active = overlayState.activeWidgetEditGuide.has_value() &&
            MatchesWidgetEditGuide(guide, *overlayState.activeWidgetEditGuide);
        SelectObject(hdc, active ? activePen : pen);
        MoveToEx(hdc, guide.drawStart.x, guide.drawStart.y, nullptr);
        LineTo(hdc, guide.drawEnd.x, guide.drawEnd.y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(activePen);
    DeleteObject(pen);
}

int DashboardRenderer::WidgetExtentForAxis(const ResolvedWidgetLayout& widget, LayoutGuideAxis axis) const {
    return axis == LayoutGuideAxis::Vertical
        ? std::max(0, static_cast<int>(widget.rect.right - widget.rect.left))
        : std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top));
}

bool DashboardRenderer::IsWidgetAffectedByGuide(const ResolvedWidgetLayout& widget, const LayoutEditGuide& guide) const {
    if (!guide.renderCardId.empty() && widget.cardId != guide.renderCardId) {
        return false;
    }
    return widget.rect.left >= guide.containerRect.left &&
        widget.rect.top >= guide.containerRect.top &&
        widget.rect.right <= guide.containerRect.right &&
        widget.rect.bottom <= guide.containerRect.bottom;
}

bool DashboardRenderer::MatchesWidgetIdentity(const ResolvedWidgetLayout& widget, const LayoutWidgetIdentity& identity) const {
    return widget.cardId == identity.renderCardId &&
        widget.editCardId == identity.editCardId &&
        widget.nodePath == identity.nodePath;
}

bool DashboardRenderer::MatchesEditableTextKey(const EditableTextKey& left, const EditableTextKey& right) const {
    return left.fontRole == right.fontRole &&
        left.textId == right.textId &&
        left.widget.renderCardId == right.widget.renderCardId &&
        left.widget.editCardId == right.widget.editCardId &&
        left.widget.nodePath == right.widget.nodePath;
}

bool DashboardRenderer::MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) const {
    return left.axis == right.axis &&
        left.renderCardId == right.renderCardId &&
        left.editCardId == right.editCardId &&
        left.nodePath == right.nodePath &&
        left.separatorIndex == right.separatorIndex;
}

bool DashboardRenderer::MatchesEditableAnchorKey(const EditableAnchorKey& left, const EditableAnchorKey& right) const {
    return left.parameter == right.parameter &&
        left.anchorId == right.anchorId &&
        left.widget.renderCardId == right.widget.renderCardId &&
        left.widget.editCardId == right.widget.editCardId &&
        left.widget.nodePath == right.widget.nodePath;
}

bool DashboardRenderer::MatchesWidgetEditGuide(const WidgetEditGuide& left, const WidgetEditGuide& right) const {
    return left.axis == right.axis &&
        left.parameter == right.parameter &&
        left.guideId == right.guideId &&
        left.widget.renderCardId == right.widget.renderCardId &&
        left.widget.editCardId == right.widget.editCardId &&
        left.widget.nodePath == right.widget.nodePath;
}

DashboardRenderer::EditableTextBinding DashboardRenderer::MakeEditableTextBinding(
    const ResolvedWidgetLayout& widget, FontRole fontRole, int textId, int fontSize) const {
    return EditableTextBinding{
        EditableTextKey{
            LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            fontRole,
            textId,
        },
        fontSize,
    };
}

void DashboardRenderer::RegisterEditableAnchorRegion(
    const EditableAnchorKey& key, const RECT& targetRect, const RECT& anchorRect, AnchorShape shape,
    LayoutGuideAxis dragAxis, int value) {
    if (anchorRect.right <= anchorRect.left || anchorRect.bottom <= anchorRect.top) {
        return;
    }
    EditableAnchorRegion region;
    region.key = key;
    region.targetRect = targetRect;
    region.anchorRect = anchorRect;
    const int anchorHitInset = std::max(3, ScaleLogical(4));
    region.anchorHitRect = RECT{
        region.anchorRect.left - anchorHitInset,
        region.anchorRect.top - anchorHitInset,
        region.anchorRect.right + anchorHitInset,
        region.anchorRect.bottom + anchorHitInset
    };
    region.shape = shape;
    region.dragAxis = dragAxis;
    region.value = value;
    editableAnchorRegions_.push_back(std::move(region));
}

void DashboardRenderer::DrawLayoutSimilarityIndicators(HDC hdc, const EditOverlayState& overlayState) const {
    const int threshold = LayoutSimilarityThreshold();
    if (threshold <= 0) {
        return;
    }

    struct SimilarityTypeKey {
        WidgetKind kind = WidgetKind::Unknown;
        int extent = 0;

        bool operator<(const SimilarityTypeKey& other) const {
            if (kind != other.kind) {
                return kind < other.kind;
            }
            return extent < other.extent;
        }
    };

    LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
    const char* axisLabel = "horizontal";
    std::vector<const ResolvedWidgetLayout*> affectedWidgets;
    std::vector<const ResolvedWidgetLayout*> allWidgets;
    if (overlayState.similarityIndicatorMode == SimilarityIndicatorMode::AllHorizontal) {
        axis = LayoutGuideAxis::Vertical;
        axisLabel = "horizontal";
        allWidgets = CollectSimilarityIndicatorWidgets(axis);
        affectedWidgets = allWidgets;
    } else if (overlayState.similarityIndicatorMode == SimilarityIndicatorMode::AllVertical) {
        axis = LayoutGuideAxis::Horizontal;
        axisLabel = "vertical";
        allWidgets = CollectSimilarityIndicatorWidgets(axis);
        affectedWidgets = allWidgets;
    } else {
        if (!overlayState.activeLayoutEditGuide.has_value()) {
            return;
        }
        const LayoutEditGuide& guide = *overlayState.activeLayoutEditGuide;
        axis = guide.axis;
        axisLabel = axis == LayoutGuideAxis::Vertical ? "horizontal" : "vertical";
        allWidgets = CollectSimilarityIndicatorWidgets(axis);
        for (const ResolvedWidgetLayout* widget : allWidgets) {
            if (IsWidgetAffectedByGuide(*widget, guide)) {
                affectedWidgets.push_back(widget);
            }
        }
    }
    if (affectedWidgets.empty()) {
        return;
    }

    std::set<const ResolvedWidgetLayout*> visibleWidgets;
    std::map<const ResolvedWidgetLayout*, SimilarityTypeKey> exactTypeByWidget;
    for (const ResolvedWidgetLayout* affected : affectedWidgets) {
        const int affectedExtent = WidgetExtentForAxis(*affected, axis);
        if (affectedExtent <= 0) {
            continue;
        }
        const SimilarityTypeKey typeKey{affected->kind, affectedExtent};
        bool hasExactMatch = false;
        for (const ResolvedWidgetLayout* candidate : allWidgets) {
            if (candidate == affected || candidate->kind != affected->kind) {
                continue;
            }
            const int candidateExtent = WidgetExtentForAxis(*candidate, axis);
            if (candidateExtent <= 0 || std::abs(candidateExtent - affectedExtent) > threshold) {
                continue;
            }
            visibleWidgets.insert(affected);
            visibleWidgets.insert(candidate);
            if (candidateExtent == affectedExtent) {
                hasExactMatch = true;
                exactTypeByWidget.try_emplace(candidate, typeKey);
            }
        }
        if (hasExactMatch) {
            exactTypeByWidget.try_emplace(affected, typeKey);
        }
    }

    std::map<SimilarityTypeKey, int> exactTypeOrdinals;
    int nextOrdinal = 1;
    for (const ResolvedWidgetLayout* widget : allWidgets) {
        if (!visibleWidgets.contains(widget)) {
            continue;
        }
        const auto exactIt = exactTypeByWidget.find(widget);
        if (exactIt == exactTypeByWidget.end() || exactTypeOrdinals.contains(exactIt->second)) {
            continue;
        }
        exactTypeOrdinals[exactIt->second] = nextOrdinal++;
    }

    std::vector<SimilarityIndicator> indicators;
    indicators.reserve(visibleWidgets.size());
    for (const ResolvedWidgetLayout* widget : allWidgets) {
        if (!visibleWidgets.contains(widget)) {
            continue;
        }
        const auto exactIt = exactTypeByWidget.find(widget);
        const int exactTypeOrdinal = exactIt == exactTypeByWidget.end()
            ? 0
            : exactTypeOrdinals[exactIt->second];
        indicators.push_back(SimilarityIndicator{
            axis,
            widget->rect,
            exactTypeOrdinal,
        });
    }
    if (indicators.empty()) {
        return;
    }

    if (traceOutput_ != nullptr) {
        for (const auto& entry : exactTypeOrdinals) {
            WriteTrace("renderer:layout_similarity_group axis=\"" + std::string(axisLabel) +
                "\" kind=" + std::to_string(static_cast<int>(entry.first.kind)) +
                " extent=" + std::to_string(entry.first.extent) +
                " ordinal=" + std::to_string(entry.second));
        }
    }

    const COLORREF color = LayoutGuideColor();
    const int inset = std::max(2, ScaleLogical(4));
    const int cap = std::max(3, ScaleLogical(4));
    const int offset = std::max(4, ScaleLogical(6));
    const int notchDepth = std::max(3, ScaleLogical(4));
    const int notchSpacing = std::max(3, ScaleLogical(4));

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    for (const SimilarityIndicator& indicator : indicators) {
        const RECT& rect = indicator.rect;
        if (indicator.axis == LayoutGuideAxis::Vertical) {
            const int y = rect.top + offset;
            const int left = rect.left + inset;
            const int right = rect.right - inset;
            MoveToEx(hdc, left, y, nullptr);
            LineTo(hdc, right, y);
            MoveToEx(hdc, left + cap, y - cap, nullptr);
            LineTo(hdc, left, y);
            LineTo(hdc, left + cap, y + cap + 1);
            MoveToEx(hdc, right - cap, y - cap, nullptr);
            LineTo(hdc, right, y);
            LineTo(hdc, right - cap, y + cap + 1);
            if (indicator.exactTypeOrdinal > 0) {
                const int cx = left + std::max(0, (right - left) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalWidth = (count - 1) * notchSpacing;
                int notchX = cx - (totalWidth / 2);
                for (int i = 0; i < count; ++i) {
                    MoveToEx(hdc, notchX, y - notchDepth, nullptr);
                    LineTo(hdc, notchX, y + notchDepth + 1);
                    notchX += notchSpacing;
                }
            }
        } else {
            const int x = rect.left + offset;
            const int top = rect.top + inset;
            const int bottom = rect.bottom - inset;
            MoveToEx(hdc, x, top, nullptr);
            LineTo(hdc, x, bottom);
            MoveToEx(hdc, x - cap, top + cap, nullptr);
            LineTo(hdc, x, top);
            LineTo(hdc, x + cap + 1, top + cap);
            MoveToEx(hdc, x - cap, bottom - cap, nullptr);
            LineTo(hdc, x, bottom);
            LineTo(hdc, x + cap + 1, bottom - cap);
            if (indicator.exactTypeOrdinal > 0) {
                const int cy = top + std::max(0, (bottom - top) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalHeight = (count - 1) * notchSpacing;
                int notchY = cy - (totalHeight / 2);
                for (int i = 0; i < count; ++i) {
                    MoveToEx(hdc, x - notchDepth, notchY, nullptr);
                    LineTo(hdc, x + notchDepth + 1, notchY);
                    notchY += notchSpacing;
                }
            }
        }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardRenderer::DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect) {
    const auto it = std::find_if(panelIcons_.begin(), panelIcons_.end(), [&](const auto& entry) {
        return entry.first == iconName;
    });
    if (it == panelIcons_.end() || it->second == nullptr) {
        return;
    }
    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage(it->second.get(),
        static_cast<INT>(iconRect.left),
        static_cast<INT>(iconRect.top),
        static_cast<INT>(iconRect.right - iconRect.left),
        static_cast<INT>(iconRect.bottom - iconRect.top));
}

void DashboardRenderer::DrawPanel(HDC hdc, const ResolvedCardLayout& card) {
    HPEN border = CreatePen(PS_SOLID, std::max(1, ScaleLogical(config_.layout.cardStyle.cardBorderWidth)),
        ToColorRef(config_.layout.colors.panelBorderColor));
    HBRUSH fill = CreateSolidBrush(ToColorRef(config_.layout.colors.panelFillColor));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    const int radius = std::max(1, ScaleLogical(config_.layout.cardStyle.cardRadius));
    RoundRect(hdc, card.rect.left, card.rect.top, card.rect.right, card.rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(border);
    if (!card.iconName.empty()) {
        DrawPanelIcon(hdc, card.iconName, card.iconRect);
    }
    if (!card.title.empty()) {
        DrawTextBlock(hdc, card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            EditableTextBinding{
                EditableTextKey{
                    LayoutWidgetIdentity{card.id, card.id, {}},
                    FontRole::Title,
                    0,
                },
                config_.layout.fonts.title.size,
            });
    }
}

void DashboardRenderer::DrawGauge(HDC hdc, const ResolvedWidgetLayout& widget, int cx, int cy, int radius,
    const DashboardGaugeMetric& metric, const std::string& label) {
    const float segmentThickness = static_cast<float>(std::max(1, ScaleLogical(config_.layout.gauge.ringThickness)));
    const GaugeSegmentLayout gaugeLayout = ComputeGaugeSegmentLayout(
        config_.layout.gauge.sweepDegrees,
        config_.layout.gauge.segmentCount,
        config_.layout.gauge.segmentGapDegrees);
    const int segmentCount = gaugeLayout.segmentCount;
    const double clampedPercent = std::clamp(metric.percent, 0.0, 100.0);
    const int filledSegments = clampedPercent <= 0.0 ? 0
        : std::clamp(static_cast<int>(std::ceil(clampedPercent * static_cast<double>(segmentCount) / 100.0)),
            1, segmentCount);
    const double clampedPeakRatio = std::clamp(metric.peakRatio, 0.0, 1.0);
    const int peakSegment = clampedPeakRatio <= 0.0 ? -1
        : std::clamp(static_cast<int>(std::ceil(clampedPeakRatio * static_cast<double>(segmentCount))) - 1,
            0, segmentCount - 1);
    const float segmentRadius = static_cast<float>(radius);
    const int anchorSize = std::max(4, ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    const int outerRadius = radius + static_cast<int>(std::ceil(static_cast<double>(segmentThickness) / 2.0f));
    const RECT anchorRect{
        cx - anchorHalf,
        cy - outerRadius - anchorHalf,
        cx - anchorHalf + anchorSize,
        cy - outerRadius - anchorHalf + anchorSize
    };
    RegisterEditableAnchorRegion(EditableAnchorKey{
        LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
        AnchorEditParameter::SegmentCount,
        0,
    }, widget.rect, anchorRect, AnchorShape::Diamond, LayoutGuideAxis::Vertical, config_.layout.gauge.segmentCount);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const Gdiplus::Color trackColor(255, GetRValue(ToColorRef(config_.layout.colors.trackColor)),
        GetGValue(ToColorRef(config_.layout.colors.trackColor)), GetBValue(ToColorRef(config_.layout.colors.trackColor)));
    const Gdiplus::Color usageColor(255, GetRValue(AccentColor()), GetGValue(AccentColor()), GetBValue(AccentColor()));
    const Gdiplus::Color ghostColor(96, GetRValue(AccentColor()), GetGValue(AccentColor()), GetBValue(AccentColor()));

    for (int i = 0; i < segmentCount; ++i) {
        const double slotStart = gaugeLayout.gaugeStart + gaugeLayout.pitchSweep * static_cast<double>(i);
        FillGaugeSegment(graphics, static_cast<float>(cx), static_cast<float>(cy), segmentRadius,
            segmentThickness, slotStart, gaugeLayout.segmentSweep, trackColor);

        if (renderMode_ != RenderMode::Blank && i < filledSegments) {
            FillGaugeSegment(graphics, static_cast<float>(cx), static_cast<float>(cy), segmentRadius,
                segmentThickness, slotStart, gaugeLayout.segmentSweep, usageColor);
        }

        if (renderMode_ != RenderMode::Blank && i == peakSegment) {
            FillGaugeSegment(graphics, static_cast<float>(cx), static_cast<float>(cy), segmentRadius,
                segmentThickness, slotStart, gaugeLayout.segmentSweep, ghostColor);
        }
    }

    const int halfWidth = std::max(1, ScaleLogical(config_.layout.gauge.textHalfWidth));
    if (renderMode_ != RenderMode::Blank) {
        RECT numberRect{cx - halfWidth,
            cy - ScaleLogical(config_.layout.gauge.valueTop),
            cx + halfWidth,
            cy + ScaleLogical(config_.layout.gauge.valueBottom)};
        char number[16];
        sprintf_s(number, "%.0f%%", metric.percent);
        DrawTextBlock(hdc, numberRect, number, fonts_.big, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            MakeEditableTextBinding(widget, FontRole::Big, 0, config_.layout.fonts.big.size));
    }
    RECT labelRect{cx - halfWidth,
        cy + ScaleLogical(config_.layout.gauge.labelTop),
        cx + halfWidth,
        cy + ScaleLogical(config_.layout.gauge.labelBottom)};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        MakeEditableTextBinding(widget, FontRole::Small, 1, config_.layout.fonts.smallText.size));
}

void DashboardRenderer::DrawPillBar(HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    FillCapsule(hdc, rect, ToColorRef(config_.layout.colors.trackColor), 255);

    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!drawFill) {
        return;
    }

    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);
    const int straightWidth = std::max(0, width - height);
    const int fillWidth = std::min(width, height + static_cast<int>(std::round(clampedRatio * straightWidth)));
    RECT fillRect = rect;
    fillRect.right = fillRect.left + fillWidth;
    FillCapsule(hdc, fillRect, AccentColor(), 255);

    if (peakRatio.has_value()) {
        const double peak = std::clamp(*peakRatio, 0.0, 1.0);
        const int markerWidth = std::min(width, std::max(1, std::max(ScaleLogical(4), height)));
        const int centerX = static_cast<int>(rect.left) + static_cast<int>(std::round(peak * width));
        const int minLeft = static_cast<int>(rect.left);
        const int maxLeft = static_cast<int>(rect.right) - markerWidth;
        const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
        RECT markerRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
        FillCapsule(hdc, markerRect, AccentColor(), 96);
    }
}

void DashboardRenderer::DrawMetricRow(HDC hdc, const ResolvedWidgetLayout& widget, const RECT& rect,
    const DashboardMetricRow& row, int rowIndex) {
    const int rowHeight = EffectiveMetricRowHeight();
    const int labelWidth = std::max(1, ScaleLogical(config_.layout.metricList.labelWidth));
    const int valueGap = std::max(0, ScaleLogical(config_.layout.metricList.valueGap));
    RECT labelRect{rect.left, rect.top, std::min(rect.right, rect.left + labelWidth), rect.bottom};
    RECT valueRect{std::min(rect.right, labelRect.right + valueGap), rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, row.label, fonts_.label, MutedTextColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER,
        MakeEditableTextBinding(widget, FontRole::Label, rowIndex * 2, config_.layout.fonts.label.size));
    if (renderMode_ != RenderMode::Blank) {
        DrawTextBlock(hdc, valueRect, row.valueText, fonts_.value, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            MakeEditableTextBinding(widget, FontRole::Value, rowIndex * 2 + 1, config_.layout.fonts.value.size));
    }

    const int metricBarHeight = std::max(1, ScaleLogical(config_.layout.metricList.barHeight));
    const int barBottom = std::min(static_cast<int>(rect.bottom), static_cast<int>(rect.top) + rowHeight);
    const int barTop = std::max(static_cast<int>(rect.top), barBottom - metricBarHeight);
    RECT barRect{valueRect.left, barTop, rect.right, barBottom};
    DrawPillBar(hdc, barRect, row.ratio, row.peakRatio, renderMode_ != RenderMode::Blank);
    const int metricBarAnchorSize = std::max(4, ScaleLogical(6));
    const int metricBarAnchorCenterX = static_cast<int>(barRect.left) + std::max(0, static_cast<int>(barRect.right - barRect.left) / 2);
    const int metricBarAnchorCenterY = static_cast<int>(barRect.bottom);
    RECT metricBarAnchorRect{
        metricBarAnchorCenterX - (metricBarAnchorSize / 2),
        metricBarAnchorCenterY - (metricBarAnchorSize / 2),
        metricBarAnchorCenterX - (metricBarAnchorSize / 2) + metricBarAnchorSize,
        metricBarAnchorCenterY - (metricBarAnchorSize / 2) + metricBarAnchorSize
    };
    RegisterEditableAnchorRegion(EditableAnchorKey{
        LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
        AnchorEditParameter::MetricListBarHeight,
        rowIndex,
    }, barRect, metricBarAnchorRect, AnchorShape::Circle, LayoutGuideAxis::Horizontal, config_.layout.metricList.barHeight);
}

void DashboardRenderer::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue,
    double guideStepMbps, double timeMarkerOffsetSamples, double timeMarkerIntervalSamples,
    const std::optional<EditableTextBinding>& maxLabelEditable) {
    HBRUSH bg = CreateSolidBrush(ToColorRef(config_.layout.colors.graphBackgroundColor));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    const int axisWidth = std::max(1, measuredWidths_.throughputAxis);
    const int labelBandHeight = std::max(
        fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.throughput.scaleLabelPadding)),
        std::max(1, ScaleLogical(config_.layout.throughput.scaleLabelMinHeight)));
    const int graphTop = std::min(rect.bottom - 1, rect.top + labelBandHeight);
    const int graphLeft = rect.left + axisWidth;
    const int leaderDiameter = std::max(0, ScaleLogical(config_.layout.throughput.leaderDiameter));
    const int leaderRadius = leaderDiameter / 2;
    const int width = std::max<int>(1, rect.right - graphLeft - 1 - leaderRadius);
    const int graphRight = graphLeft + width;
    const int graphBottom = rect.bottom - 1;
    const int plotStrokeWidth = std::max(1, ScaleLogical(config_.layout.throughput.plotStrokeWidth));
    const int plotTop = std::min(graphBottom, static_cast<int>(rect.top) + plotStrokeWidth);
    const int plotHeight = std::max(1, graphBottom - plotTop);

    const int strokeWidth = std::max(1, ScaleLogical(config_.layout.throughput.guideStrokeWidth));
    const double guideStep = guideStepMbps > 0.0 ? guideStepMbps : 5.0;
    HBRUSH markerBrush = CreateSolidBrush(ToColorRef(config_.layout.colors.graphMarkerColor));
    for (double tick = guideStep; tick < maxValue; tick += guideStep) {
        const double ratio = tick / maxValue;
        const int y = graphBottom - static_cast<int>(std::round(ratio * plotHeight));
        RECT lineRect{graphLeft, std::max(plotTop, y), graphRight, std::min(graphBottom + 1, y + strokeWidth)};
        FillRect(hdc, &lineRect, markerBrush);
    }

    if (!history.empty()) {
        const double markerInterval = timeMarkerIntervalSamples > 0.0 ? timeMarkerIntervalSamples : 20.0;
        for (double sampleOffset = timeMarkerOffsetSamples;
             sampleOffset <= static_cast<double>(history.size() - 1) + markerInterval;
             sampleOffset += markerInterval) {
            const double clampedOffset = std::clamp(sampleOffset, 0.0, static_cast<double>(history.size() - 1));
            const int x = graphRight - static_cast<int>(std::round(
                clampedOffset * width / std::max<size_t>(1, history.size() - 1)));
            RECT lineRect{x, rect.top, std::min(graphRight + 1, x + strokeWidth), rect.bottom};
            FillRect(hdc, &lineRect, markerBrush);
        }
    }

    DeleteObject(markerBrush);

    HBRUSH axisBrush = CreateSolidBrush(ToColorRef(config_.layout.colors.graphAxisColor));
    RECT verticalAxisRect{rect.left + axisWidth, rect.top, rect.left + axisWidth + strokeWidth, rect.bottom};
    RECT horizontalAxisRect{rect.left + axisWidth, rect.bottom - strokeWidth, rect.right, rect.bottom};
    FillRect(hdc, &verticalAxisRect, axisBrush);
    FillRect(hdc, &horizontalAxisRect, axisBrush);
    DeleteObject(axisBrush);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top, rect.left + axisWidth, graphTop};
    if (renderMode_ != RenderMode::Blank) {
        DrawTextBlock(hdc, maxRect, maxLabel, fonts_.smallFont, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            maxLabelEditable);
    }

    if (renderMode_ == RenderMode::Blank) {
        return;
    }

    const COLORREF plotColor = AccentColor();
    HPEN pen = CreatePen(PS_SOLID, plotStrokeWidth, plotColor);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    POINT lastPoint{graphLeft, graphBottom};
    bool hasLastPoint = false;
    if (!history.empty()) {
        const size_t historyDenominator = std::max<size_t>(1, history.size() - 1);
        for (size_t i = 0; i < history.size(); ++i) {
            const double valueRatio = std::clamp(history[i] / maxValue, 0.0, 1.0);
            const int x = graphLeft + static_cast<int>(i * width / historyDenominator);
            const int y = graphBottom - static_cast<int>(std::round(valueRatio * plotHeight));
            lastPoint = POINT{x, y};
            hasLastPoint = true;
        }
    }
    for (size_t i = 1; i < history.size(); ++i) {
        const double v1 = std::clamp(history[i - 1] / maxValue, 0.0, 1.0);
        const double v2 = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x1 = graphLeft + static_cast<int>((i - 1) * width / std::max<size_t>(1, history.size() - 1));
        const int x2 = graphLeft + static_cast<int>(i * width / std::max<size_t>(1, history.size() - 1));
        const int y1 = graphBottom - static_cast<int>(std::round(v1 * plotHeight));
        const int y2 = graphBottom - static_cast<int>(std::round(v2 * plotHeight));
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    if (hasLastPoint && leaderDiameter > 0) {
        FillCircle(hdc, lastPoint.x, lastPoint.y, leaderDiameter, plotColor, 255);
    }
}

void DashboardRenderer::DrawThroughputWidget(HDC hdc, const ResolvedWidgetLayout& widget, const RECT& rect,
    const DashboardThroughputMetric& metric) {
    const int lineHeight = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.throughput.valuePadding));
    RECT valueRect{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + lineHeight)};
    RECT graphRect{rect.left, std::min(rect.bottom, valueRect.bottom + std::max(0, ScaleLogical(config_.layout.throughput.headerGap))),
        rect.right, rect.bottom};
    const int labelWidth = std::max(1, measuredWidths_.throughputLabel);
    RECT labelRect{valueRect.left, valueRect.top, std::min(valueRect.right, valueRect.left + labelWidth), valueRect.bottom};
    RECT numberRect{std::min(valueRect.right, labelRect.right + std::max(0, ScaleLogical(config_.layout.throughput.headerGap))),
        valueRect.top, valueRect.right, valueRect.bottom};
    char buffer[64];
    if (metric.valueMbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", metric.valueMbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", metric.valueMbps);
    }
    DrawTextBlock(hdc, labelRect, metric.label, fonts_.smallFont, MutedTextColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER,
        MakeEditableTextBinding(widget, FontRole::Small, 0, config_.layout.fonts.smallText.size));
    if (renderMode_ != RenderMode::Blank) {
        DrawTextBlock(hdc, numberRect, buffer, fonts_.smallFont, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            MakeEditableTextBinding(widget, FontRole::Small, 1, config_.layout.fonts.smallText.size));
    }
    DrawGraph(hdc, graphRect, metric.history, metric.maxGraph, metric.guideStepMbps,
        metric.timeMarkerOffsetSamples, metric.timeMarkerIntervalSamples,
        MakeEditableTextBinding(widget, FontRole::Small, 2, config_.layout.fonts.smallText.size));
}

void DashboardRenderer::DrawDriveUsageWidget(HDC hdc, const ResolvedWidgetLayout& widget, const RECT& rect,
    const std::vector<DashboardDriveRow>& rows) {
    const int headerHeight = EffectiveDriveHeaderHeight();
    const int rowHeight = EffectiveDriveRowHeight();
    const int labelWidth = std::max(1, measuredWidths_.driveLabel);
    const int percentWidth = std::max(1, measuredWidths_.drivePercent);
    const int freeWidth = std::max(1, ScaleLogical(config_.layout.driveUsageList.freeWidth));
    const int activityWidth = std::max(1, ScaleLogical(config_.layout.driveUsageList.activityWidth));
    const int barGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.barGap));
    const int valueGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.valueGap));
    const int percentGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.percentGap));
    const int driveBarHeight = std::max(1, ScaleLogical(config_.layout.driveUsageList.barHeight));
    const int activitySegments = std::max(1, config_.layout.driveUsageList.activitySegments);
    const int activitySegmentGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.activitySegmentGap));
    const int rowContentHeight = std::max(fontHeights_.label, std::max(fontHeights_.smallText, driveBarHeight));

    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, rect.left, rect.top, rect.right, rect.bottom);

    RECT header{rect.left, rect.top, rect.right, rect.top + headerHeight};
    RECT row{rect.left, header.bottom, rect.right, header.bottom + rowHeight};

    const auto resolveColumns = [&](const RECT& band, RECT& labelRect, RECT& readRect, RECT& writeRect,
        RECT& barRect, RECT& pctRect, RECT& freeRect) {
        labelRect = {band.left, band.top, std::min(band.right, static_cast<LONG>(band.left + labelWidth)), band.bottom};
        readRect = {std::min(band.right, static_cast<LONG>(labelRect.right + barGap)), band.top,
            std::min(band.right, static_cast<LONG>(labelRect.right + barGap + activityWidth)), band.bottom};
        writeRect = {std::min(band.right, static_cast<LONG>(readRect.right + valueGap)), band.top,
            std::min(band.right, static_cast<LONG>(readRect.right + valueGap + activityWidth)), band.bottom};
        freeRect = {std::max(band.left, static_cast<LONG>(band.right - freeWidth)), band.top, band.right, band.bottom};
        pctRect = {std::max(band.left, static_cast<LONG>(freeRect.left - valueGap - percentWidth)), band.top,
            std::max(band.left, static_cast<LONG>(freeRect.left - valueGap)), band.bottom};
        barRect = {std::min(band.right, static_cast<LONG>(writeRect.right + barGap)), band.top,
            std::max(std::min(band.right, static_cast<LONG>(writeRect.right + barGap)),
                static_cast<LONG>(pctRect.left - percentGap)), band.bottom};
    };

    RECT headerLabelRect{}, headerReadRect{}, headerWriteRect{}, headerBarRect{}, headerPctRect{}, headerFreeRect{};
    resolveColumns(header, headerLabelRect, headerReadRect, headerWriteRect, headerBarRect, headerPctRect, headerFreeRect);
    const int activityAnchorWidth = std::max(8, ScaleLogical(14));
    const int activityAnchorHeight = std::max(6, ScaleLogical(8));
    const int activityAnchorCenterX = headerReadRect.left + std::max(0L, headerWriteRect.right - headerReadRect.left) / 2;
    const int activityAnchorBandTop = std::min(static_cast<int>(rect.bottom), static_cast<int>(header.bottom));
    const int activityAnchorBandBottom = std::max(activityAnchorBandTop, static_cast<int>(rect.bottom));
    const int activityAnchorCenterY = activityAnchorBandTop +
        std::max(0, (activityAnchorBandBottom - activityAnchorBandTop) / 2);
    RECT activityAnchorRect{
        activityAnchorCenterX - (activityAnchorWidth / 2),
        activityAnchorCenterY - (activityAnchorHeight / 2),
        activityAnchorCenterX - (activityAnchorWidth / 2) + activityAnchorWidth,
        activityAnchorCenterY - (activityAnchorHeight / 2) + activityAnchorHeight
    };
    RegisterEditableAnchorRegion(EditableAnchorKey{
        LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
        AnchorEditParameter::DriveUsageActivitySegments,
        0,
    }, RECT{headerReadRect.left, rect.top, headerWriteRect.right, rect.bottom}, activityAnchorRect,
        AnchorShape::Diamond, LayoutGuideAxis::Vertical,
        config_.layout.driveUsageList.activitySegments);
    RECT usageHeaderRect{headerBarRect.left, header.top, headerPctRect.right, header.bottom};
    RECT headerReadLabelRect{headerReadRect.left - valueGap, headerReadRect.top, headerReadRect.right + valueGap, headerReadRect.bottom};
    RECT headerWriteLabelRect{headerWriteRect.left - valueGap, headerWriteRect.top, headerWriteRect.right + valueGap, headerWriteRect.bottom};
    DrawTextBlock(hdc, headerReadLabelRect, "R", fonts_.smallFont, MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP,
        MakeEditableTextBinding(widget, FontRole::Small, 0, config_.layout.fonts.smallText.size));
    DrawTextBlock(hdc, headerWriteLabelRect, "W", fonts_.smallFont, MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP,
        MakeEditableTextBinding(widget, FontRole::Small, 1, config_.layout.fonts.smallText.size));
    DrawTextBlock(hdc, usageHeaderRect, "Usage", fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        MakeEditableTextBinding(widget, FontRole::Small, 2, config_.layout.fonts.smallText.size));
    DrawTextBlock(hdc, headerFreeRect, "Free", fonts_.smallFont, MutedTextColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
        MakeEditableTextBinding(widget, FontRole::Small, 3, config_.layout.fonts.smallText.size));

    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& drive = rows[rowIndex];
        const int textBaseId = 100 + static_cast<int>(rowIndex) * 3;
        RECT labelRect{}, readRect{}, writeRect{}, pctRect{}, freeRect{}, barBandRect{};
        resolveColumns(row, labelRect, readRect, writeRect, barBandRect, pctRect, freeRect);
        const int rowPixelHeight = static_cast<int>(row.bottom - row.top);
        const int contentTop = static_cast<int>(row.top) + std::max(0, (rowPixelHeight - rowContentHeight) / 2);
        RECT activityRect{0, contentTop, 0, contentTop + rowContentHeight};
        RECT readIndicatorRect{readRect.left, activityRect.top, readRect.right, activityRect.bottom};
        RECT writeIndicatorRect{writeRect.left, activityRect.top, writeRect.right, activityRect.bottom};
        const int barTop = static_cast<int>(row.top) + std::max(0, (rowPixelHeight - driveBarHeight) / 2);
        RECT barRect{
            barBandRect.left,
            barTop,
            barBandRect.right,
            barTop + driveBarHeight
        };

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            MakeEditableTextBinding(widget, FontRole::Label, textBaseId, config_.layout.fonts.label.size));
        DrawSegmentIndicator(hdc, readIndicatorRect, activitySegments, activitySegmentGap,
            renderMode_ == RenderMode::Blank ? 0.0 : drive.readActivity,
            ToColorRef(config_.layout.colors.trackColor), AccentColor());
        DrawSegmentIndicator(hdc, writeIndicatorRect, activitySegments, activitySegmentGap,
            renderMode_ == RenderMode::Blank ? 0.0 : drive.writeActivity,
            ToColorRef(config_.layout.colors.trackColor), AccentColor());
        DrawPillBar(hdc, barRect, drive.usedPercent / 100.0, std::nullopt, renderMode_ != RenderMode::Blank);
        const int driveBarAnchorSize = std::max(4, ScaleLogical(6));
        const int driveBarAnchorCenterX = static_cast<int>(barRect.left) + std::max(0, static_cast<int>(barRect.right - barRect.left) / 2);
        const int driveBarAnchorCenterY = static_cast<int>(barRect.bottom);
        RECT driveBarAnchorRect{
            driveBarAnchorCenterX - (driveBarAnchorSize / 2),
            driveBarAnchorCenterY - (driveBarAnchorSize / 2),
            driveBarAnchorCenterX - (driveBarAnchorSize / 2) + driveBarAnchorSize,
            driveBarAnchorCenterY - (driveBarAnchorSize / 2) + driveBarAnchorSize
        };
        RegisterEditableAnchorRegion(EditableAnchorKey{
            LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            AnchorEditParameter::DriveUsageBarHeight,
            static_cast<int>(rowIndex),
        }, barRect, driveBarAnchorRect, AnchorShape::Circle, LayoutGuideAxis::Horizontal,
            config_.layout.driveUsageList.barHeight);

        if (renderMode_ != RenderMode::Blank) {
            char percent[16];
            sprintf_s(percent, "%.0f%%", drive.usedPercent);
            DrawTextBlock(hdc, pctRect, percent, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                MakeEditableTextBinding(widget, FontRole::Label, textBaseId + 1, config_.layout.fonts.label.size));
            DrawTextBlock(hdc, freeRect, drive.freeText, fonts_.smallFont, MutedTextColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
                MakeEditableTextBinding(widget, FontRole::Small, textBaseId + 2, config_.layout.fonts.smallText.size));
        }

        OffsetRect(&row, 0, rowHeight);
        if (row.top >= rect.bottom) {
            break;
        }
    }

    RestoreDC(hdc, savedDc);
}

void DashboardRenderer::DrawResolvedWidget(HDC hdc, const ResolvedWidgetLayout& widget, const DashboardMetricSource& metrics) {
    switch (widget.kind) {
    case WidgetKind::Text:
        DrawTextBlock(hdc, widget.rect, metrics.ResolveText(widget.binding.metric), fonts_.text, ForegroundColor(),
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS,
            MakeEditableTextBinding(widget, FontRole::Text, 0, config_.layout.fonts.text.size));
        return;
    case WidgetKind::Gauge: {
        const DashboardGaugeMetric gaugeMetric = metrics.ResolveGauge(widget.binding.metric);
        const int width = widget.rect.right - widget.rect.left;
        const int height = widget.rect.bottom - widget.rect.top;
        const int radius = std::min(std::max(1, resolvedLayout_.globalGaugeRadius), GaugeRadiusForRect(widget.rect));
        DrawGauge(hdc, widget, widget.rect.left + width / 2, widget.rect.top + height / 2, radius, gaugeMetric, "Load");
        return;
    }
    case WidgetKind::MetricList: {
        const int rowHeight = EffectiveMetricRowHeight();
        const int savedDc = SaveDC(hdc);
        IntersectClipRect(hdc, widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.bottom);
        RECT rowRect{widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.top + rowHeight};
        int rowIndex = 0;
        for (const auto& row : metrics.ResolveMetricList(ParseMetricListEntries(widget.binding.param))) {
            DrawMetricRow(hdc, widget, rowRect, row, rowIndex++);
            OffsetRect(&rowRect, 0, rowHeight);
            if (rowRect.top >= widget.rect.bottom) {
                break;
            }
        }
        RestoreDC(hdc, savedDc);
        return;
    }
    case WidgetKind::Throughput:
        DrawThroughputWidget(hdc, widget, widget.rect, metrics.ResolveThroughput(widget.binding.metric));
        return;
    case WidgetKind::NetworkFooter:
        if (renderMode_ != RenderMode::Blank) {
            DrawTextBlock(hdc, widget.rect, metrics.ResolveNetworkFooter(), fonts_.footer, MutedTextColor(),
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS,
                MakeEditableTextBinding(widget, FontRole::Footer, 0, config_.layout.fonts.footer.size));
        }
        return;
    case WidgetKind::Spacer:
    case WidgetKind::VerticalSpring:
        return;
    case WidgetKind::DriveUsageList:
        DrawDriveUsageWidget(hdc, widget, widget.rect, metrics.ResolveDriveRows());
        return;
    case WidgetKind::ClockTime:
        if (renderMode_ != RenderMode::Blank) {
            DrawTextBlock(hdc, widget.rect, metrics.ResolveClockTime(), fonts_.clockTime, ForegroundColor(),
                DT_CENTER | DT_SINGLELINE | DT_VCENTER,
                MakeEditableTextBinding(widget, FontRole::ClockTime, 0, config_.layout.fonts.clockTime.size));
        }
        return;
    case WidgetKind::ClockDate:
        if (renderMode_ != RenderMode::Blank) {
            DrawTextBlock(hdc, widget.rect, metrics.ResolveClockDate(), fonts_.clockDate, MutedTextColor(),
                DT_CENTER | DT_SINGLELINE | DT_VCENTER,
                MakeEditableTextBinding(widget, FontRole::ClockDate, 0, config_.layout.fonts.clockDate.size));
        }
        return;
    default:
        return;
    }
}

void DashboardRenderer::Draw(HDC hdc, const SystemSnapshot& snapshot) {
    Draw(hdc, snapshot, EditOverlayState{});
}

void DashboardRenderer::Draw(HDC hdc, const SystemSnapshot& snapshot, const EditOverlayState& overlayState) {
    editableTextRegions_.clear();
    editableAnchorRegions_.clear();
    DashboardMetricSource metrics(snapshot, config_.metricScales);
    for (const auto& card : resolvedLayout_.cards) {
        DrawPanel(hdc, card);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidget(hdc, widget, metrics);
        }
    }
    DrawHoveredWidgetHighlight(hdc, overlayState);
    DrawHoveredEditableTextHighlight(hdc, overlayState);
    DrawHoveredEditableAnchorHighlight(hdc, overlayState);
    DrawLayoutEditGuides(hdc, overlayState);
    DrawWidgetEditGuides(hdc, overlayState);
    DrawLayoutSimilarityIndicators(hdc, overlayState);
}

bool DashboardRenderer::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    return SaveSnapshotPng(imagePath, snapshot, EditOverlayState{});
}

bool DashboardRenderer::SaveSnapshotPng(
    const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const EditOverlayState& overlayState) {
    if (!Initialize(hwnd_)) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        lastError_ = "renderer:screenshot_getdc_failed " + FormatWin32Error(GetLastError());
        return false;
    }
    HDC memDc = CreateCompatibleDC(screenDc);
    if (memDc == nullptr) {
        lastError_ = "renderer:screenshot_create_compatible_dc_failed " + FormatWin32Error(GetLastError());
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = WindowWidth();
    bitmapInfo.bmiHeader.biHeight = -WindowHeight();
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap == nullptr) {
        lastError_ = "renderer:screenshot_create_dib_failed " + FormatWin32Error(GetLastError());
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
    RECT client{0, 0, WindowWidth(), WindowHeight()};
    HBRUSH background = CreateSolidBrush(BackgroundColor());
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);
    Draw(memDc, snapshot, overlayState);

    CLSID pngClsid{};
    Gdiplus::Bitmap image(bitmap, nullptr);
    const int encoderIndex = GetImageEncoderClsid(L"image/png", &pngClsid);
    Gdiplus::Status saveStatus = Gdiplus::GenericError;
    bool saved = false;
    if (encoderIndex >= 0) {
        saveStatus = image.Save(imagePath.c_str(), &pngClsid, nullptr);
        saved = saveStatus == Gdiplus::Ok;
    }

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    if (!saved) {
        if (encoderIndex < 0) {
            lastError_ = "renderer:screenshot_encoder_missing mime=\"image/png\"";
        } else {
            lastError_ = "renderer:screenshot_save_failed status=" + std::to_string(static_cast<int>(saveStatus)) +
                " path=\"" + Utf8FromWide(imagePath.wstring()) + "\"";
        }
    }
    return saved;
}

int DashboardRenderer::ScaleLogical(int value) const {
    if (value <= 0) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(value) * renderScale_)));
}
