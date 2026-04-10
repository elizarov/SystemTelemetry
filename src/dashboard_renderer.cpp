#include "dashboard_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <objidl.h>
#include <optional>
#include <set>
#include <vector>

#include <gdiplus.h>

#include "utf8.h"

namespace {

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
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
DashboardRenderer::TextLayoutResult DashboardRenderer::MeasureTextBlock(
    HDC hdc, const RECT& rect, const std::string& text, HFONT font, UINT format) const {
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

    result.textRect = RECT{left,
        top,
        std::min(rect.right, static_cast<LONG>(left + textWidth)),
        std::min(rect.bottom, static_cast<LONG>(top + textHeight))};
    SelectObject(hdc, oldFont);
    return result;
}

DashboardRenderer::TextLayoutResult DashboardRenderer::DrawTextBlock(HDC hdc,
    const RECT& rect,
    const std::string& text,
    HFONT font,
    COLORREF color,
    UINT format,
    const std::optional<EditableAnchorBinding>& editable) {
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
        const RECT anchorRect{anchorCenterX - anchorHalf,
            anchorCenterY - anchorHalf,
            anchorCenterX - anchorHalf + anchorSize,
            anchorCenterY - anchorHalf + anchorSize};
        RegisterEditableAnchorRegion(editable->key,
            result.textRect,
            anchorRect,
            editable->shape,
            editable->dragAxis,
            editable->dragMode,
            false,
            true,
            editable->value);
    }
    return result;
}

void DashboardRenderer::DrawHoveredWidgetHighlight(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || !overlayState.hoveredEditableWidget.has_value()) {
        return;
    }

    const DashboardWidgetLayout* hoveredWidget = nullptr;
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
    Rectangle(
        hdc, hoveredWidget->rect.left, hoveredWidget->rect.top, hoveredWidget->rect.right, hoveredWidget->rect.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardRenderer::DrawHoveredEditableAnchorHighlight(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides) {
        return;
    }

    std::vector<std::pair<const EditableAnchorRegion*, bool>> highlights;
    if (overlayState.activeEditableAnchor.has_value()) {
        const auto it = std::find_if(
            editableAnchorRegions_.begin(), editableAnchorRegions_.end(), [&](const EditableAnchorRegion& region) {
                return MatchesEditableAnchorKey(region.key, *overlayState.activeEditableAnchor);
            });
        if (it != editableAnchorRegions_.end()) {
            highlights.push_back({&(*it), true});
        }
    } else if (overlayState.hoveredEditableAnchor.has_value()) {
        const auto it = std::find_if(
            editableAnchorRegions_.begin(), editableAnchorRegions_.end(), [&](const EditableAnchorRegion& region) {
                return MatchesEditableAnchorKey(region.key, *overlayState.hoveredEditableAnchor);
            });
        if (it != editableAnchorRegions_.end()) {
            highlights.push_back({&(*it), false});
        }
    } else if (overlayState.hoveredEditableWidget.has_value()) {
        for (const auto& region : editableAnchorRegions_) {
            if (!region.showWhenWidgetHovered) {
                continue;
            }
            if (region.key.widget.renderCardId != overlayState.hoveredEditableWidget->renderCardId ||
                region.key.widget.editCardId != overlayState.hoveredEditableWidget->editCardId ||
                region.key.widget.nodePath != overlayState.hoveredEditableWidget->nodePath) {
                continue;
            }
            highlights.push_back({&region, false});
        }
    }
    if (highlights.empty()) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    for (const auto& [highlighted, active] : highlights) {
        const COLORREF outlineColor = active ? ActiveEditColor() : LayoutGuideColor();
        if (highlighted->drawTargetOutline && highlighted->targetRect.right > highlighted->targetRect.left &&
            highlighted->targetRect.bottom > highlighted->targetRect.top) {
            Gdiplus::Pen pen(
                Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)),
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
            Gdiplus::SolidBrush fill(
                Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)));
            graphics.FillEllipse(&fill,
                static_cast<Gdiplus::REAL>(anchorRect.left),
                static_cast<Gdiplus::REAL>(anchorRect.top),
                static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.right - anchorRect.left)),
                static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.bottom - anchorRect.top)));
        } else {
            FillDiamond(hdc, highlighted->anchorRect, outlineColor);
        }
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

int DashboardRenderer::WidgetExtentForAxis(const DashboardWidgetLayout& widget, LayoutGuideAxis axis) const {
    return axis == LayoutGuideAxis::Vertical ? std::max(0, static_cast<int>(widget.rect.right - widget.rect.left))
                                             : std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top));
}

bool DashboardRenderer::IsWidgetAffectedByGuide(
    const DashboardWidgetLayout& widget, const LayoutEditGuide& guide) const {
    if (!guide.renderCardId.empty() && widget.cardId != guide.renderCardId) {
        return false;
    }
    return widget.rect.left >= guide.containerRect.left && widget.rect.top >= guide.containerRect.top &&
           widget.rect.right <= guide.containerRect.right && widget.rect.bottom <= guide.containerRect.bottom;
}

bool DashboardRenderer::MatchesWidgetIdentity(
    const DashboardWidgetLayout& widget, const LayoutWidgetIdentity& identity) const {
    return widget.cardId == identity.renderCardId && widget.editCardId == identity.editCardId &&
           widget.nodePath == identity.nodePath;
}

bool DashboardRenderer::MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) const {
    return left.axis == right.axis && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath && left.separatorIndex == right.separatorIndex;
}

bool DashboardRenderer::MatchesEditableAnchorKey(const EditableAnchorKey& left, const EditableAnchorKey& right) const {
    return left.parameter == right.parameter && left.anchorId == right.anchorId &&
           left.widget.renderCardId == right.widget.renderCardId && left.widget.editCardId == right.widget.editCardId &&
           left.widget.nodePath == right.widget.nodePath;
}

bool DashboardRenderer::MatchesWidgetEditGuide(const WidgetEditGuide& left, const WidgetEditGuide& right) const {
    return left.axis == right.axis && left.parameter == right.parameter && left.guideId == right.guideId &&
           left.widget.renderCardId == right.widget.renderCardId && left.widget.editCardId == right.widget.editCardId &&
           left.widget.nodePath == right.widget.nodePath;
}

DashboardRenderer::EditableAnchorBinding DashboardRenderer::MakeEditableTextBinding(
    const DashboardWidgetLayout& widget, AnchorEditParameter parameter, int anchorId, int value) const {
    return EditableAnchorBinding{
        EditableAnchorKey{
            LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            parameter,
            anchorId,
        },
        value,
        AnchorShape::Circle,
        AnchorDragAxis::Vertical,
        AnchorDragMode::AxisDelta,
    };
}

void DashboardRenderer::RegisterEditableAnchorRegion(const EditableAnchorKey& key,
    const RECT& targetRect,
    const RECT& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    if (anchorRect.right <= anchorRect.left || anchorRect.bottom <= anchorRect.top) {
        return;
    }
    EditableAnchorRegion region;
    region.key = key;
    region.targetRect = targetRect;
    region.anchorRect = anchorRect;
    region.shape = shape;
    const int anchorHitInset = std::max(3, ScaleLogical(4));
    region.anchorHitRect = RECT{region.anchorRect.left - anchorHitInset,
        region.anchorRect.top - anchorHitInset,
        region.anchorRect.right + anchorHitInset,
        region.anchorRect.bottom + anchorHitInset};
    region.dragAxis = dragAxis;
    region.dragMode = dragMode;
    region.showWhenWidgetHovered = showWhenWidgetHovered;
    region.drawTargetOutline = drawTargetOutline;
    region.value = value;
    editableAnchorRegions_.push_back(std::move(region));
}

void DashboardRenderer::DrawLayoutSimilarityIndicators(HDC hdc, const EditOverlayState& overlayState) const {
    const int threshold = LayoutSimilarityThreshold();
    if (threshold <= 0) {
        return;
    }

    struct SimilarityTypeKey {
        DashboardWidgetClass widgetClass = DashboardWidgetClass::Unknown;
        int extent = 0;

        bool operator<(const SimilarityTypeKey& other) const {
            if (widgetClass != other.widgetClass) {
                return widgetClass < other.widgetClass;
            }
            return extent < other.extent;
        }
    };

    LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
    const char* axisLabel = "horizontal";
    std::vector<const DashboardWidgetLayout*> affectedWidgets;
    std::vector<const DashboardWidgetLayout*> allWidgets;
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
        for (const DashboardWidgetLayout* widget : allWidgets) {
            if (IsWidgetAffectedByGuide(*widget, guide)) {
                affectedWidgets.push_back(widget);
            }
        }
    }
    if (affectedWidgets.empty()) {
        return;
    }

    std::set<const DashboardWidgetLayout*> visibleWidgets;
    std::map<const DashboardWidgetLayout*, SimilarityTypeKey> exactTypeByWidget;
    for (const DashboardWidgetLayout* affected : affectedWidgets) {
        const int affectedExtent = WidgetExtentForAxis(*affected, axis);
        if (affectedExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        const SimilarityTypeKey typeKey{affected->widget->Class(), affectedExtent};
        bool hasExactMatch = false;
        for (const DashboardWidgetLayout* candidate : allWidgets) {
            if (candidate == affected || candidate->widget == nullptr ||
                candidate->widget->Class() != affected->widget->Class()) {
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
    for (const DashboardWidgetLayout* widget : allWidgets) {
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
    for (const DashboardWidgetLayout* widget : allWidgets) {
        if (!visibleWidgets.contains(widget)) {
            continue;
        }
        const auto exactIt = exactTypeByWidget.find(widget);
        const int exactTypeOrdinal = exactIt == exactTypeByWidget.end() ? 0 : exactTypeOrdinals[exactIt->second];
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
                       "\" class=" + std::to_string(static_cast<int>(entry.first.widgetClass)) +
                       " extent=" + std::to_string(entry.first.extent) + " ordinal=" + std::to_string(entry.second));
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
    const auto it = std::find_if(
        panelIcons_.begin(), panelIcons_.end(), [&](const auto& entry) { return entry.first == iconName; });
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
    HPEN border = CreatePen(PS_SOLID,
        std::max(1, ScaleLogical(config_.layout.cardStyle.cardBorderWidth)),
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
        DrawTextBlock(hdc,
            card.titleRect,
            card.title,
            fonts_.title,
            ForegroundColor(),
            DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            EditableAnchorBinding{
                EditableAnchorKey{
                    LayoutWidgetIdentity{card.id, card.id, {}},
                    AnchorEditParameter::FontTitle,
                    0,
                },
                config_.layout.fonts.title.size,
                AnchorShape::Circle,
                AnchorDragAxis::Vertical,
                AnchorDragMode::AxisDelta,
            });
    }
}

void DashboardRenderer::DrawPillBar(
    HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
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

void DashboardRenderer::DrawResolvedWidget(
    HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) {
    if (widget.widget == nullptr) {
        return;
    }
    widget.widget->Draw(*this, hdc, widget, metrics);
}

void DashboardRenderer::Draw(HDC hdc, const SystemSnapshot& snapshot) {
    Draw(hdc, snapshot, EditOverlayState{});
}

void DashboardRenderer::Draw(HDC hdc, const SystemSnapshot& snapshot, const EditOverlayState& overlayState) {
    editableAnchorRegions_.clear();
    DashboardMetricSource metrics(snapshot, config_.metricScales);
    for (const auto& card : resolvedLayout_.cards) {
        DrawPanel(hdc, card);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidget(hdc, widget, metrics);
        }
    }
    DrawHoveredWidgetHighlight(hdc, overlayState);
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
