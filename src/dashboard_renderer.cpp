#include "dashboard_renderer.h"
#include "dashboard_layout_resolver.h"
#include "layout_edit_service.h"
#include "layout_edit_parameter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <objidl.h>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <gdiplus.h>
#include <wincodec.h>

#include "../resources/resource.h"
#include "trace.h"
#include "utf8.h"

namespace {

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
}

D2D1_COLOR_F ToD2DColor(COLORREF color, BYTE alpha = 255) {
    constexpr float kChannelScale = 1.0f / 255.0f;
    return D2D1::ColorF(static_cast<float>(GetRValue(color)) * kChannelScale,
        static_cast<float>(GetGValue(color)) * kChannelScale,
        static_cast<float>(GetBValue(color)) * kChannelScale,
        static_cast<float>(alpha) * kChannelScale);
}

std::string FormatHresult(HRESULT hr) {
    char buffer[16];
    sprintf_s(buffer, "%08X", static_cast<unsigned int>(hr));
    return buffer;
}

DWRITE_TEXT_ALIGNMENT DWriteTextAlignment(UINT format) {
    if ((format & DT_CENTER) != 0) {
        return DWRITE_TEXT_ALIGNMENT_CENTER;
    }
    if ((format & DT_RIGHT) != 0) {
        return DWRITE_TEXT_ALIGNMENT_TRAILING;
    }
    return DWRITE_TEXT_ALIGNMENT_LEADING;
}

DWRITE_PARAGRAPH_ALIGNMENT DWriteParagraphAlignment(UINT format) {
    if ((format & DT_VCENTER) != 0) {
        return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    }
    if ((format & DT_BOTTOM) != 0) {
        return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
    }
    return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
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

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool FontConfigEquals(const UiFontConfig& left, const UiFontConfig& right) {
    return left.face == right.face && left.size == right.size && left.weight == right.weight;
}

bool FontSetConfigEquals(const UiFontSetConfig& left, const UiFontSetConfig& right) {
    return FontConfigEquals(left.title, right.title) && FontConfigEquals(left.big, right.big) &&
           FontConfigEquals(left.value, right.value) && FontConfigEquals(left.label, right.label) &&
           FontConfigEquals(left.text, right.text) && FontConfigEquals(left.smallText, right.smallText) &&
           FontConfigEquals(left.footer, right.footer) && FontConfigEquals(left.clockTime, right.clockTime) &&
           FontConfigEquals(left.clockDate, right.clockDate);
}

UINT GetPanelIconResourceId(const std::string& iconName) {
    if (iconName == "cpu")
        return IDR_PANEL_ICON_CPU;
    if (iconName == "gpu")
        return IDR_PANEL_ICON_GPU;
    if (iconName == "network")
        return IDR_PANEL_ICON_NETWORK;
    if (iconName == "storage")
        return IDR_PANEL_ICON_STORAGE;
    if (iconName == "time")
        return IDR_PANEL_ICON_TIME;
    return 0;
}

HFONT CreateUiFont(const UiFontConfig& font) {
    const std::wstring face = WideFromUtf8(font.face);
    return CreateFontW(-font.size,
        0,
        0,
        0,
        font.weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        face.c_str());
}

std::unique_ptr<Gdiplus::Bitmap> LoadPngResourceBitmap(UINT resourceId) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return nullptr;
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), L"PNG");
    if (resource == nullptr) {
        return nullptr;
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    HGLOBAL loadedResource = LoadResource(module, resource);
    if (loadedResource == nullptr || resourceSize == 0) {
        return nullptr;
    }

    const void* resourceData = LockResource(loadedResource);
    if (resourceData == nullptr) {
        return nullptr;
    }

    HGLOBAL copyHandle = GlobalAlloc(GMEM_MOVEABLE, resourceSize);
    if (copyHandle == nullptr) {
        return nullptr;
    }

    void* copyData = GlobalLock(copyHandle);
    if (copyData == nullptr) {
        GlobalFree(copyHandle);
        return nullptr;
    }

    memcpy(copyData, resourceData, resourceSize);
    GlobalUnlock(copyHandle);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(copyHandle, TRUE, &stream) != S_OK || stream == nullptr) {
        GlobalFree(copyHandle);
        return nullptr;
    }

    std::unique_ptr<Gdiplus::Bitmap> decoded(Gdiplus::Bitmap::FromStream(stream));
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    if (decoded != nullptr && decoded->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Rect rect(0, 0, decoded->GetWidth(), decoded->GetHeight());
        bitmap.reset(decoded->Clone(rect, PixelFormat32bppARGB));
        if (bitmap != nullptr && bitmap->GetLastStatus() != Gdiplus::Ok) {
            bitmap.reset();
        }
    }

    stream->Release();
    return bitmap;
}

}  // namespace

DashboardRenderer::DashboardRenderer() = default;

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

void DashboardRenderer::SetConfig(const AppConfig& config) {
    InvalidateMetricSourceCache();
    const bool fontsChanged = !FontSetConfigEquals(config_.layout.fonts, config.layout.fonts);
    config_ = config;
    if (fontsChanged) {
        DestroyFonts();
        if (!CreateFonts()) {
            lastError_ = "renderer:font_create_failed";
            return;
        }
    }
    if (fonts_.title != nullptr && fonts_.big != nullptr && fonts_.value != nullptr && fonts_.label != nullptr &&
        fonts_.text != nullptr && fonts_.smallFont != nullptr && fonts_.footer != nullptr &&
        fonts_.clockTime != nullptr && fonts_.clockDate != nullptr) {
        if (!MeasureFonts() || !ResolveLayout()) {
            lastError_ = lastError_.empty() ? "renderer:reconfigure_failed" : lastError_;
        } else {
            d2dFirstDrawWarmupPending_ = true;
        }
    }
}

void DashboardRenderer::SetRenderScale(double scale) {
    const double nextScale = std::clamp(scale, 0.1, 16.0);
    if (std::abs(renderScale_ - nextScale) < 0.0001) {
        return;
    }
    renderScale_ = nextScale;
    if (fonts_.title != nullptr && fonts_.big != nullptr && fonts_.value != nullptr && fonts_.label != nullptr &&
        fonts_.text != nullptr && fonts_.smallFont != nullptr && fonts_.footer != nullptr &&
        fonts_.clockTime != nullptr && fonts_.clockDate != nullptr) {
        DestroyFonts();
        if (!CreateFonts()) {
            lastError_ = "renderer:font_create_failed";
            return;
        }
        if (!MeasureFonts() || !ResolveLayout()) {
            lastError_ = lastError_.empty() ? "renderer:rescale_failed" : lastError_;
        } else {
            d2dFirstDrawWarmupPending_ = true;
        }
    }
}

void DashboardRenderer::SetRenderMode(RenderMode mode) {
    renderMode_ = mode;
}

void DashboardRenderer::SetLayoutGuideDragActive(bool active) {
    layoutGuideDragActive_ = active;
}

void DashboardRenderer::SetInteractiveDragTraceActive(bool active) {
    interactiveDragTraceActive_ = active;
}

void DashboardRenderer::RebuildEditArtifacts() {
    BuildWidgetEditGuides();
    BuildStaticEditableAnchors();
}

bool DashboardRenderer::SetLayoutEditPreviewWidgetType(
    EditOverlayState& overlayState, const std::string& widgetTypeName) const {
    const auto widget = FindFirstLayoutEditPreviewWidget(widgetTypeName);
    if (!widget.has_value()) {
        return false;
    }
    overlayState.hoveredEditableWidget = widget;
    return true;
}

double DashboardRenderer::RenderScale() const {
    return renderScale_;
}

const std::string& DashboardRenderer::LastError() const {
    return lastError_;
}

const AppConfig& DashboardRenderer::Config() const {
    return config_;
}

const DashboardRenderer::FontHeights& DashboardRenderer::FontMetrics() const {
    return fontHeights_;
}

const DashboardRenderer::Fonts& DashboardRenderer::WidgetFonts() const {
    return fonts_;
}

DashboardRenderer::RenderMode DashboardRenderer::CurrentRenderMode() const {
    return renderMode_;
}

bool DashboardRenderer::IsDirect2DActive() const {
    return d2dDrawActive_ && d2dActiveRenderTarget_ != nullptr;
}

COLORREF DashboardRenderer::TrackColor() const {
    return ToColorRef(config_.layout.colors.trackColor);
}

std::vector<DashboardRenderer::WidgetEditGuide>& DashboardRenderer::WidgetEditGuidesMutable() {
    return widgetEditGuides_;
}

int DashboardRenderer::WindowWidth() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.width));
}

int DashboardRenderer::WindowHeight() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.height));
}

COLORREF DashboardRenderer::BackgroundColor() const {
    return ToColorRef(config_.layout.colors.backgroundColor);
}

COLORREF DashboardRenderer::ForegroundColor() const {
    return ToColorRef(config_.layout.colors.foregroundColor);
}

COLORREF DashboardRenderer::AccentColor() const {
    return ToColorRef(config_.layout.colors.accentColor);
}

COLORREF DashboardRenderer::LayoutGuideColor() const {
    return ToColorRef(config_.layout.colors.layoutGuideColor);
}

COLORREF DashboardRenderer::ActiveEditColor() const {
    return ToColorRef(config_.layout.colors.activeEditColor);
}

COLORREF DashboardRenderer::MutedTextColor() const {
    return ToColorRef(config_.layout.colors.mutedTextColor);
}

HFONT DashboardRenderer::LabelFont() const {
    return fonts_.label;
}

HFONT DashboardRenderer::SmallFont() const {
    return fonts_.smallFont;
}

void DashboardRenderer::SetTraceOutput(std::ostream* traceOutput) {
    traceOutput_ = traceOutput;
}

const std::vector<DashboardRenderer::LayoutEditGuide>& DashboardRenderer::LayoutEditGuides() const {
    return layoutEditGuides_;
}

const std::vector<DashboardRenderer::WidgetEditGuide>& DashboardRenderer::WidgetEditGuides() const {
    return widgetEditGuides_;
}

int DashboardRenderer::LayoutSimilarityThreshold() const {
    return std::max(0, ScaleLogical(config_.layout.layoutEditor.sizeSimilarityThreshold));
}

void DashboardRenderer::ResolveNodeWidgets(const LayoutNodeConfig& node,
    const RECT& rect,
    std::vector<DashboardWidgetLayout>& widgets,
    bool instantiateWidgets) {
    DashboardLayoutResolver::ResolveNodeWidgets(*this, node, rect, widgets, instantiateWidgets);
}

void DashboardRenderer::BuildWidgetEditGuides() {
    DashboardLayoutResolver::BuildWidgetEditGuides(*this);
}

void DashboardRenderer::BuildStaticEditableAnchors() {
    DashboardLayoutResolver::BuildStaticEditableAnchors(*this);
}

void DashboardRenderer::AddLayoutEditGuide(const LayoutNodeConfig& node,
    const RECT& rect,
    const std::vector<RECT>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    DashboardLayoutResolver::AddLayoutEditGuide(*this, node, rect, childRects, gap, renderCardId, editCardId, nodePath);
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
    const RECT& rect,
    std::vector<DashboardWidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    bool instantiateWidgets) {
    DashboardLayoutResolver::ResolveNodeWidgetsInternal(
        *this, node, rect, widgets, cardReferenceStack, renderCardId, editCardId, nodePath, instantiateWidgets);
}

bool DashboardRenderer::ResolveLayout(bool includeWidgetState) {
    return DashboardLayoutResolver::ResolveLayout(*this, includeWidgetState);
}

DashboardRenderer::TextLayoutResult DashboardRenderer::MeasureTextBlock(
    const RECT& rect, const std::string& text, HFONT font, UINT format) const {
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return TextLayoutResult{rect};
    }
    return MeasureTextBlockD2D(rect, wideText, font, format, nullptr);
}

DashboardRenderer::TextLayoutResult DashboardRenderer::DrawTextBlock(
    const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) {
    TextLayoutResult result{rect};
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return result;
    }
    result = MeasureTextBlockD2D(rect, wideText, font, format, nullptr);
    IDWriteTextFormat* textFormat = DWriteTextFormatForFont(font);
    if (textFormat == nullptr) {
        return result;
    }
    ConfigureDWriteTextFormat(textFormat, format);
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return result;
    }
    const D2D1_DRAW_TEXT_OPTIONS options =
        (format & DT_NOCLIP) != 0 ? D2D1_DRAW_TEXT_OPTIONS_NONE : D2D1_DRAW_TEXT_OPTIONS_CLIP;
    d2dActiveRenderTarget_->DrawText(wideText.c_str(),
        static_cast<UINT32>(wideText.size()),
        textFormat,
        D2D1::RectF(static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right),
            static_cast<float>(rect.bottom)),
        brush,
        options);
    return result;
}

void DashboardRenderer::DrawText(
    const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) const {
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return;
    }
    IDWriteTextFormat* textFormat = DWriteTextFormatForFont(font);
    if (textFormat == nullptr) {
        return;
    }
    ConfigureDWriteTextFormat(textFormat, format);
    ID2D1SolidColorBrush* brush = const_cast<DashboardRenderer*>(this)->D2DSolidBrush(color);
    if (brush == nullptr) {
        return;
    }
    const D2D1_DRAW_TEXT_OPTIONS options =
        (format & DT_NOCLIP) != 0 ? D2D1_DRAW_TEXT_OPTIONS_NONE : D2D1_DRAW_TEXT_OPTIONS_CLIP;
    d2dActiveRenderTarget_->DrawText(wideText.c_str(),
        static_cast<UINT32>(wideText.size()),
        textFormat,
        D2D1::RectF(static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right),
            static_cast<float>(rect.bottom)),
        brush,
        options);
}

void DashboardRenderer::DrawHoveredWidgetHighlight(const EditOverlayState& overlayState) const {
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
    const_cast<DashboardRenderer*>(this)->DrawSolidRect(hoveredWidget->rect, LayoutGuideColor());
}

void DashboardRenderer::DrawHoveredEditableAnchorHighlight(const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides) {
        return;
    }

    std::vector<std::pair<EditableAnchorRegion, bool>> highlights;
    const auto appendHighlight = [&](const EditableAnchorRegion& region, bool active) {
        const auto existing = std::find_if(highlights.begin(), highlights.end(), [&](const auto& entry) {
            return MatchesEditableAnchorKey(entry.first.key, region.key);
        });
        if (existing == highlights.end()) {
            highlights.push_back({region, active});
            return;
        }
        existing->second = existing->second || active;
    };
    const auto appendByKey = [&](const std::optional<EditableAnchorKey>& key, bool active) {
        if (!key.has_value()) {
            return;
        }
        const auto region = FindEditableAnchorRegion(*key);
        if (region.has_value()) {
            appendHighlight(*region, active);
        }
    };
    if (overlayState.hoveredEditableWidget.has_value()) {
        const auto collectHovered = [&](const std::vector<EditableAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered) {
                    continue;
                }
                if (region.key.widget.renderCardId != overlayState.hoveredEditableWidget->renderCardId ||
                    region.key.widget.editCardId != overlayState.hoveredEditableWidget->editCardId ||
                    region.key.widget.nodePath != overlayState.hoveredEditableWidget->nodePath) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHovered(staticEditableAnchorRegions_);
        collectHovered(dynamicEditableAnchorRegions_);
    }
    appendByKey(overlayState.hoveredEditableAnchor, false);
    appendByKey(overlayState.activeEditableAnchor, true);
    if (highlights.empty()) {
        return;
    }
    for (const auto& [highlighted, active] : highlights) {
        const COLORREF outlineColor = active ? ActiveEditColor() : LayoutGuideColor();
        if (highlighted.drawTargetOutline && highlighted.targetRect.right > highlighted.targetRect.left &&
            highlighted.targetRect.bottom > highlighted.targetRect.top) {
            RECT outlineRect = highlighted.targetRect;
            InflateRect(&outlineRect, std::max(1, ScaleLogical(1)), std::max(1, ScaleLogical(1)));
            const_cast<DashboardRenderer*>(this)->DrawSolidRect(
                outlineRect, outlineColor, std::max(1, ScaleLogical(1)), true);
        }

        if (highlighted.shape == AnchorShape::Circle) {
            const_cast<DashboardRenderer*>(this)->DrawSolidEllipse(
                highlighted.anchorRect, outlineColor, std::max(1, ScaleLogical(1)));
        } else {
            const_cast<DashboardRenderer*>(this)->FillSolidDiamond(highlighted.anchorRect, outlineColor);
        }
    }
}

void DashboardRenderer::DrawLayoutEditGuides(const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || layoutEditGuides_.empty()) {
        return;
    }

    for (const auto& guide : layoutEditGuides_) {
        const bool active = overlayState.activeLayoutEditGuide.has_value() &&
                            MatchesLayoutEditGuide(guide, *overlayState.activeLayoutEditGuide);
        const COLORREF color = active ? ActiveEditColor() : LayoutGuideColor();
        const POINT start{guide.lineRect.left, guide.lineRect.top};
        const POINT end = guide.axis == LayoutGuideAxis::Vertical ? POINT{guide.lineRect.left, guide.lineRect.bottom}
                                                                  : POINT{guide.lineRect.right, guide.lineRect.top};
        const_cast<DashboardRenderer*>(this)->DrawSolidLine(start, end, color);
    }
}

void DashboardRenderer::DrawWidgetEditGuides(const EditOverlayState& overlayState) const {
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

    for (const auto& guide : widgetEditGuides_) {
        if (!shouldDraw(guide)) {
            continue;
        }
        const bool active = overlayState.activeWidgetEditGuide.has_value() &&
                            MatchesWidgetEditGuide(guide, *overlayState.activeWidgetEditGuide);
        const COLORREF color = active ? ActiveEditColor() : LayoutGuideColor();
        const_cast<DashboardRenderer*>(this)->DrawSolidLine(guide.drawStart, guide.drawEnd, color);
    }
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
    const DashboardWidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const {
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

void DashboardRenderer::RegisterEditableAnchorRegion(std::vector<EditableAnchorRegion>& regions,
    const EditableAnchorKey& key,
    const RECT& targetRect,
    const RECT& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    POINT dragOrigin,
    double dragScale,
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
    region.anchorHitPadding = anchorHitInset;
    region.anchorHitRect = RECT{region.anchorRect.left - anchorHitInset,
        region.anchorRect.top - anchorHitInset,
        region.anchorRect.right + anchorHitInset,
        region.anchorRect.bottom + anchorHitInset};
    region.dragAxis = dragAxis;
    region.dragMode = dragMode;
    region.dragOrigin = dragOrigin;
    region.dragScale = dragScale;
    region.showWhenWidgetHovered = showWhenWidgetHovered;
    region.drawTargetOutline = drawTargetOutline;
    region.value = value;
    regions.push_back(std::move(region));
}

void DashboardRenderer::RegisterStaticEditableAnchorRegion(const EditableAnchorKey& key,
    const RECT& targetRect,
    const RECT& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    POINT dragOrigin,
    double dragScale,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    RegisterEditableAnchorRegion(staticEditableAnchorRegions_,
        key,
        targetRect,
        anchorRect,
        shape,
        dragAxis,
        dragMode,
        dragOrigin,
        dragScale,
        showWhenWidgetHovered,
        drawTargetOutline,
        value);
}

void DashboardRenderer::RegisterDynamicEditableAnchorRegion(const EditableAnchorKey& key,
    const RECT& targetRect,
    const RECT& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    POINT dragOrigin,
    double dragScale,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterEditableAnchorRegion(dynamicEditableAnchorRegions_,
        key,
        targetRect,
        anchorRect,
        shape,
        dragAxis,
        dragMode,
        dragOrigin,
        dragScale,
        showWhenWidgetHovered,
        drawTargetOutline,
        value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<EditableAnchorRegion>& regions,
    const RECT& rect,
    const std::string& text,
    HFONT font,
    UINT format,
    const EditableAnchorBinding& editable) {
    if (text.empty()) {
        return;
    }

    const TextLayoutResult result = MeasureTextBlock(rect, text, font, format);
    const int anchorSize = std::max(4, ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    const int anchorCenterX = result.textRect.right;
    const int anchorCenterY = result.textRect.top;
    const RECT anchorRect{anchorCenterX - anchorHalf,
        anchorCenterY - anchorHalf,
        anchorCenterX - anchorHalf + anchorSize,
        anchorCenterY - anchorHalf + anchorSize};
    RegisterEditableAnchorRegion(regions,
        editable.key,
        result.textRect,
        anchorRect,
        editable.shape,
        editable.dragAxis,
        editable.dragMode,
        POINT{anchorCenterX, anchorCenterY},
        1.0,
        false,
        true,
        editable.value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<EditableAnchorRegion>& regions,
    const TextLayoutResult& layoutResult,
    const EditableAnchorBinding& editable) {
    const RECT& textRect = layoutResult.textRect;
    if (textRect.right <= textRect.left || textRect.bottom <= textRect.top) {
        return;
    }

    const int anchorSize = std::max(4, ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    const int anchorCenterX = textRect.right;
    const int anchorCenterY = textRect.top;
    const RECT anchorRect{anchorCenterX - anchorHalf,
        anchorCenterY - anchorHalf,
        anchorCenterX - anchorHalf + anchorSize,
        anchorCenterY - anchorHalf + anchorSize};
    RegisterEditableAnchorRegion(regions,
        editable.key,
        textRect,
        anchorRect,
        editable.shape,
        editable.dragAxis,
        editable.dragMode,
        POINT{anchorCenterX, anchorCenterY},
        1.0,
        false,
        true,
        editable.value);
}

void DashboardRenderer::RegisterStaticTextAnchor(
    const RECT& rect, const std::string& text, HFONT font, UINT format, const EditableAnchorBinding& editable) {
    RegisterTextAnchor(staticEditableAnchorRegions_, rect, text, font, format, editable);
}

void DashboardRenderer::RegisterDynamicTextAnchor(
    const TextLayoutResult& layoutResult, const EditableAnchorBinding& editable) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, layoutResult, editable);
}

void DashboardRenderer::RegisterDynamicTextAnchor(
    const RECT& rect, const std::string& text, HFONT font, UINT format, const EditableAnchorBinding& editable) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, rect, text, font, format, editable);
}

void DashboardRenderer::DrawLayoutSimilarityIndicators(const EditOverlayState& overlayState) const {
    const int threshold = LayoutSimilarityThreshold();
    if (threshold <= 0) {
        return;
    }

    struct SimilarityTypeKey {
        DashboardWidgetClass widgetClass = DashboardWidgetClass::Unknown;
        int extent = 0;

        bool operator==(const SimilarityTypeKey& other) const {
            return widgetClass == other.widgetClass && extent == other.extent;
        }

        bool operator<(const SimilarityTypeKey& other) const {
            if (widgetClass != other.widgetClass) {
                return widgetClass < other.widgetClass;
            }
            return extent < other.extent;
        }
    };

    struct SimilarityTypeKeyHash {
        size_t operator()(const SimilarityTypeKey& key) const {
            size_t hash = std::hash<int>{}(static_cast<int>(key.widgetClass));
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.extent);
            return hash;
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

    std::unordered_map<DashboardWidgetClass, std::vector<const DashboardWidgetLayout*>> widgetsByClass;
    widgetsByClass.reserve(allWidgets.size());
    for (const DashboardWidgetLayout* widget : allWidgets) {
        if (widget->widget == nullptr) {
            continue;
        }
        widgetsByClass[widget->widget->Class()].push_back(widget);
    }

    std::unordered_set<const DashboardWidgetLayout*> visibleWidgets;
    visibleWidgets.reserve(allWidgets.size());
    std::unordered_map<const DashboardWidgetLayout*, SimilarityTypeKey> exactTypeByWidget;
    exactTypeByWidget.reserve(allWidgets.size());
    for (const DashboardWidgetLayout* affected : affectedWidgets) {
        const int affectedExtent = WidgetExtentForAxis(*affected, axis);
        if (affectedExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        const SimilarityTypeKey typeKey{affected->widget->Class(), affectedExtent};
        bool hasExactMatch = false;
        const auto classIt = widgetsByClass.find(typeKey.widgetClass);
        if (classIt == widgetsByClass.end()) {
            continue;
        }
        for (const DashboardWidgetLayout* candidate : classIt->second) {
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

    std::unordered_map<SimilarityTypeKey, int, SimilarityTypeKeyHash> exactTypeOrdinals;
    exactTypeOrdinals.reserve(exactTypeByWidget.size());
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

    for (const SimilarityIndicator& indicator : indicators) {
        const RECT& rect = indicator.rect;
        if (indicator.axis == LayoutGuideAxis::Vertical) {
            const int y = rect.top + offset;
            const int left = rect.left + inset;
            const int right = rect.right - inset;
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{left, y}, POINT{right, y}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{left + cap, y - cap}, POINT{left, y}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{left, y}, POINT{left + cap, y + cap + 1}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{right - cap, y - cap}, POINT{right, y}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                POINT{right, y}, POINT{right - cap, y + cap + 1}, color);
            if (indicator.exactTypeOrdinal > 0) {
                const int cx = left + std::max(0, (right - left) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalWidth = (count - 1) * notchSpacing;
                int notchX = cx - (totalWidth / 2);
                for (int i = 0; i < count; ++i) {
                    const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                        POINT{notchX, y - notchDepth}, POINT{notchX, y + notchDepth + 1}, color);
                    notchX += notchSpacing;
                }
            }
        } else {
            const int x = rect.left + offset;
            const int top = rect.top + inset;
            const int bottom = rect.bottom - inset;
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{x, top}, POINT{x, bottom}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{x - cap, top + cap}, POINT{x, top}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{x, top}, POINT{x + cap + 1, top + cap}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(POINT{x - cap, bottom - cap}, POINT{x, bottom}, color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                POINT{x, bottom}, POINT{x + cap + 1, bottom - cap}, color);
            if (indicator.exactTypeOrdinal > 0) {
                const int cy = top + std::max(0, (bottom - top) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalHeight = (count - 1) * notchSpacing;
                int notchY = cy - (totalHeight / 2);
                for (int i = 0; i < count; ++i) {
                    const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                        POINT{x - notchDepth, notchY}, POINT{x + notchDepth + 1, notchY}, color);
                    notchY += notchSpacing;
                }
            }
        }
    }
}

void DashboardRenderer::DrawPanelIcon(const std::string& iconName, const RECT& iconRect) {
    const auto it = std::find_if(
        panelIcons_.begin(), panelIcons_.end(), [&](const auto& entry) { return entry.first == iconName; });
    if (it == panelIcons_.end() || it->second == nullptr || d2dActiveRenderTarget_ == nullptr) {
        return;
    }
    const int width = std::max(0, static_cast<int>(iconRect.right - iconRect.left));
    const int height = std::max(0, static_cast<int>(iconRect.bottom - iconRect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    const PanelIconCacheKey cacheKey{iconName, width, height};
    auto scaled = d2dPanelIconCache_.find(cacheKey);
    if (scaled == d2dPanelIconCache_.end()) {
        Gdiplus::Bitmap surface(width, height, PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(&surface);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.DrawImage(it->second.get(), 0, 0, width, height);

        Gdiplus::BitmapData bitmapData{};
        const Gdiplus::Rect lockRect(0, 0, width, height);
        if (surface.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bitmapData) !=
            Gdiplus::Ok) {
            return;
        }

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        const HRESULT hr = d2dActiveRenderTarget_->CreateBitmap(D2D1::SizeU(width, height),
            bitmapData.Scan0,
            static_cast<UINT32>(bitmapData.Stride),
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            bitmap.GetAddressOf());
        surface.UnlockBits(&bitmapData);
        if (FAILED(hr) || bitmap == nullptr) {
            return;
        }
        scaled = d2dPanelIconCache_.emplace(cacheKey, std::move(bitmap)).first;
    }

    d2dActiveRenderTarget_->DrawBitmap(scaled->second.Get(),
        D2D1::RectF(static_cast<float>(iconRect.left),
            static_cast<float>(iconRect.top),
            static_cast<float>(iconRect.right),
            static_cast<float>(iconRect.bottom)),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void DashboardRenderer::DrawPanel(const ResolvedCardLayout& card) {
    const COLORREF borderColor = ToColorRef(config_.layout.colors.panelBorderColor);
    const COLORREF fillColor = ToColorRef(config_.layout.colors.panelFillColor);
    const float radius = static_cast<float>(std::max(1, ScaleLogical(config_.layout.cardStyle.cardRadius)));
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<float>(card.rect.left),
                                                                static_cast<float>(card.rect.top),
                                                                static_cast<float>(card.rect.right),
                                                                static_cast<float>(card.rect.bottom)),
        radius,
        radius);
    ID2D1SolidColorBrush* fillBrush = D2DSolidBrush(fillColor);
    ID2D1SolidColorBrush* borderBrush = D2DSolidBrush(borderColor);
    if (fillBrush != nullptr) {
        d2dActiveRenderTarget_->FillRoundedRectangle(roundedRect, fillBrush);
    }
    if (borderBrush != nullptr) {
        d2dActiveRenderTarget_->DrawRoundedRectangle(roundedRect,
            borderBrush,
            static_cast<float>(std::max(1, ScaleLogical(config_.layout.cardStyle.cardBorderWidth))));
    }
    if (!card.iconName.empty()) {
        DrawPanelIcon(card.iconName, card.iconRect);
    }
    if (!card.title.empty()) {
        DrawText(card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

void DashboardRenderer::DrawPillBar(const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    const COLORREF accentColor = AccentColor();
    const auto fillCapsule = [&](const RECT& capsuleRect, COLORREF color, BYTE alpha = 255) {
        const int capsuleWidth = std::max(0, static_cast<int>(capsuleRect.right - capsuleRect.left));
        const int capsuleHeight = std::max(0, static_cast<int>(capsuleRect.bottom - capsuleRect.top));
        if (capsuleWidth <= 0 || capsuleHeight <= 0) {
            return;
        }
        ID2D1SolidColorBrush* brush = D2DSolidBrush(color, alpha);
        if (brush == nullptr) {
            return;
        }
        if (capsuleWidth <= capsuleHeight) {
            const float radiusX = static_cast<float>(capsuleWidth) / 2.0f;
            const float radiusY = static_cast<float>(capsuleHeight) / 2.0f;
            d2dActiveRenderTarget_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(static_cast<float>(capsuleRect.left) + radiusX,
                                  static_cast<float>(capsuleRect.top) + radiusY),
                    radiusX,
                    radiusY),
                brush);
        } else {
            const float radius = static_cast<float>(capsuleHeight) / 2.0f;
            d2dActiveRenderTarget_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(static_cast<float>(capsuleRect.left),
                                      static_cast<float>(capsuleRect.top),
                                      static_cast<float>(capsuleRect.right),
                                      static_cast<float>(capsuleRect.bottom)),
                    radius,
                    radius),
                brush);
        }
    };

    fillCapsule(rect, ToColorRef(config_.layout.colors.trackColor));

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
    fillCapsule(fillRect, accentColor);

    if (peakRatio.has_value()) {
        const double peak = std::clamp(*peakRatio, 0.0, 1.0);
        const int markerWidth = std::min(width, std::max(1, std::max(ScaleLogical(4), height)));
        const int centerX = static_cast<int>(rect.left) + static_cast<int>(std::round(peak * width));
        const int minLeft = static_cast<int>(rect.left);
        const int maxLeft = static_cast<int>(rect.right) - markerWidth;
        const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
        RECT markerRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
        fillCapsule(markerRect, accentColor, 96);
    }
}

void DashboardRenderer::DrawResolvedWidget(const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) {
    if (widget.widget == nullptr) {
        return;
    }
    widget.widget->Draw(*this, widget, metrics);
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot) {
    return DrawWindow(snapshot, EditOverlayState{});
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot, const EditOverlayState& overlayState) {
    if (!BeginWindowDraw()) {
        return false;
    }
    DrawDirect2DFrame(snapshot, overlayState);
    EndWindowDraw();
    return lastError_.empty();
}

void DashboardRenderer::DrawDirect2DFrame(const SystemSnapshot& snapshot, const EditOverlayState& overlayState) {
    if (d2dActiveRenderTarget_ == nullptr) {
        lastError_ = "renderer:d2d_target_missing";
        return;
    }

    dynamicEditableAnchorRegions_.clear();
    dynamicAnchorRegistrationEnabled_ =
        overlayState.showLayoutEditGuides && !overlayState.activeLayoutEditGuide.has_value();
    const DashboardMetricSource& metrics = ResolveMetrics(snapshot);
    d2dActiveRenderTarget_->Clear(ToD2DColor(BackgroundColor()));
    for (const auto& card : resolvedLayout_.cards) {
        DrawPanel(card);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidget(widget, metrics);
        }
    }
    DrawHoveredWidgetHighlight(overlayState);
    DrawHoveredEditableAnchorHighlight(overlayState);
    DrawLayoutEditGuides(overlayState);
    DrawWidgetEditGuides(overlayState);
    DrawLayoutSimilarityIndicators(overlayState);
    dynamicAnchorRegistrationEnabled_ = false;
}

bool DashboardRenderer::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    return SaveSnapshotPng(imagePath, snapshot, EditOverlayState{});
}

bool DashboardRenderer::SaveSnapshotPng(
    const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const EditOverlayState& overlayState) {
    if (!InitializeDirect2D()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    const UINT width = static_cast<UINT>(std::max(1, WindowWidth()));
    const UINT height = static_cast<UINT>(std::max(1, WindowHeight()));
    HRESULT hr = wicFactory_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr) || bitmap == nullptr) {
        lastError_ = "renderer:screenshot_wic_bitmap_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    Microsoft::WRL::ComPtr<ID2D1RenderTarget> bitmapRenderTarget;
    hr = d2dFactory_->CreateWicBitmapRenderTarget(bitmap.Get(),
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f),
        bitmapRenderTarget.GetAddressOf());
    if (FAILED(hr) || bitmapRenderTarget == nullptr) {
        lastError_ = "renderer:screenshot_d2d_target_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get())) {
        return false;
    }
    DrawDirect2DFrame(snapshot, overlayState);
    EndDirect2DDraw();
    if (!lastError_.empty()) {
        return false;
    }

    return SaveWicBitmapPng(bitmap.Get(), imagePath);
}

bool DashboardRenderer::SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath) {
    if (wicFactory_ == nullptr || bitmap == nullptr) {
        lastError_ = "renderer:screenshot_wic_unavailable";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT hr = wicFactory_->CreateStream(stream.GetAddressOf());
    if (FAILED(hr) || stream == nullptr) {
        lastError_ = "renderer:screenshot_stream_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = stream->InitializeFromFilename(imagePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_stream_open_failed hr=0x" + FormatHresult(hr) + " path=\"" +
                     Utf8FromWide(imagePath.wstring()) + "\"";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr) || encoder == nullptr) {
        lastError_ = "renderer:screenshot_encoder_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_encoder_init_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr) || frame == nullptr) {
        lastError_ = "renderer:screenshot_frame_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_init_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = bitmap->GetSize(&width, &height);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_bitmap_size_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_size_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_format_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = frame->WriteSource(bitmap, nullptr);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_write_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_commit_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_commit_failed hr=0x" + FormatHresult(hr);
        return false;
    }

    return true;
}

int DashboardRenderer::ScaleLogical(int value) const {
    if (value <= 0) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(value) * renderScale_)));
}

int DashboardRenderer::MeasureTextWidth(HFONT font, std::string_view text) const {
    const TextWidthCacheLookupKey cacheKey{font, text};
    if (const auto it = textWidthCache_.find(cacheKey); it != textWidthCache_.end()) {
        return it->second;
    }

    if (dwriteFactory_ == nullptr) {
        return 0;
    }

    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return 0;
    }
    const RECT measureRect{0, 0, WindowWidth(), WindowHeight()};
    const int width = std::max(0,
        static_cast<int>(MeasureTextBlockD2D(measureRect, wideText, font, DT_LEFT | DT_SINGLELINE | DT_VCENTER, nullptr)
                .textRect.right));
    textWidthCache_.emplace(TextWidthCacheKey{font, std::string(text)}, width);
    return width;
}

std::vector<DashboardRenderer::LayoutGuideSnapCandidate> DashboardRenderer::CollectLayoutGuideSnapCandidates(
    const LayoutEditGuide& guide) const {
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

    std::vector<const DashboardWidgetLayout*> allWidgets = CollectSimilarityIndicatorWidgets(guide.axis);
    std::vector<const DashboardWidgetLayout*> affectedWidgets;
    for (const DashboardWidgetLayout* widget : allWidgets) {
        if (IsWidgetAffectedByGuide(*widget, guide)) {
            affectedWidgets.push_back(widget);
        }
    }

    std::vector<LayoutGuideSnapCandidate> candidates;
    for (const DashboardWidgetLayout* affected : affectedWidgets) {
        const int startExtent = WidgetExtentForAxis(*affected, guide.axis);
        if (startExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        std::set<SimilarityTypeKey> seenTargets;
        for (size_t i = 0; i < allWidgets.size(); ++i) {
            const DashboardWidgetLayout* target = allWidgets[i];
            if (target == affected || target->widget == nullptr ||
                target->widget->Class() != affected->widget->Class()) {
                continue;
            }
            const SimilarityTypeKey typeKey{target->widget->Class(), WidgetExtentForAxis(*target, guide.axis)};
            if (!seenTargets.insert(typeKey).second) {
                continue;
            }
            candidates.push_back(LayoutGuideSnapCandidate{
                {affected->cardId, affected->editCardId, affected->nodePath},
                typeKey.extent,
                startExtent,
                std::abs(typeKey.extent - startExtent),
                i,
            });
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.startDistance != right.startDistance) {
            return left.startDistance < right.startDistance;
        }
        return left.groupOrder < right.groupOrder;
    });
    return candidates;
}

std::optional<int> DashboardRenderer::FindLayoutWidgetExtent(
    const LayoutWidgetIdentity& identity, LayoutGuideAxis axis) const {
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (MatchesWidgetIdentity(widget, identity)) {
                return WidgetExtentForAxis(widget, axis);
            }
        }
    }
    return std::nullopt;
}

bool DashboardRenderer::ApplyLayoutGuideWeightsPreview(
    const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights) {
    LayoutEditHost::LayoutTarget target;
    target.editCardId = editCardId;
    target.nodePath = nodePath;
    if (!layout_edit::ApplyGuideWeights(config_, target, weights)) {
        return false;
    }
    return ResolveLayout(false);
}

std::optional<DashboardRenderer::LayoutWidgetIdentity> DashboardRenderer::HitTestEditableWidget(
    POINT clientPoint) const {
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || !PtInRect(&widget.rect, clientPoint)) {
                continue;
            }
            return LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorKey> DashboardRenderer::HitTestEditableAnchorTarget(
    POINT clientPoint) const {
    std::vector<const EditableAnchorRegion*> regions;
    regions.reserve(staticEditableAnchorRegions_.size() + dynamicEditableAnchorRegions_.size());
    for (const auto& region : staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (PtInRect(&(*it)->targetRect, clientPoint)) {
            return (*it)->key;
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorKey> DashboardRenderer::HitTestEditableAnchorHandle(
    POINT clientPoint) const {
    std::vector<const EditableAnchorRegion*> regions;
    regions.reserve(staticEditableAnchorRegions_.size() + dynamicEditableAnchorRegions_.size());
    for (const auto& region : staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    const EditableAnchorRegion* bestRegion = nullptr;
    int bestPriority = 0;
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        const EditableAnchorRegion& region = *(*it);
        bool hit = false;
        if (region.shape == AnchorShape::Circle) {
            const int width = std::max(1L, region.anchorRect.right - region.anchorRect.left);
            const int height = std::max(1L, region.anchorRect.bottom - region.anchorRect.top);
            const double radius = static_cast<double>(std::max(width, height)) / 2.0;
            const double centerX = static_cast<double>(region.anchorRect.left) + static_cast<double>(width) / 2.0;
            const double centerY = static_cast<double>(region.anchorRect.top) + static_cast<double>(height) / 2.0;
            const double dx = static_cast<double>(clientPoint.x) - centerX;
            const double dy = static_cast<double>(clientPoint.y) - centerY;
            const double distance = std::sqrt((dx * dx) + (dy * dy));
            hit = std::abs(distance - radius) <= static_cast<double>(region.anchorHitPadding);
        } else {
            hit = PtInRect(&region.anchorHitRect, clientPoint);
        }
        if (!hit) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority(region.key.parameter);
        if (bestRegion == nullptr || priority < bestPriority) {
            bestRegion = &region;
            bestPriority = priority;
        }
    }
    return bestRegion != nullptr ? std::optional<EditableAnchorKey>(bestRegion->key) : std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorRegion> DashboardRenderer::FindEditableAnchorRegion(
    const EditableAnchorKey& key) const {
    const auto findIn = [&](const std::vector<EditableAnchorRegion>& regions) -> std::optional<EditableAnchorRegion> {
        const auto it = std::find_if(regions.begin(), regions.end(), [&](const EditableAnchorRegion& region) {
            return MatchesEditableAnchorKey(region.key, key);
        });
        if (it == regions.end()) {
            return std::nullopt;
        }
        return *it;
    };
    if (const auto staticRegion = findIn(staticEditableAnchorRegions_); staticRegion.has_value()) {
        return staticRegion;
    }
    return findIn(dynamicEditableAnchorRegions_);
}

std::optional<DashboardRenderer::LayoutWidgetIdentity> DashboardRenderer::FindFirstLayoutEditPreviewWidget(
    const std::string& widgetTypeName) const {
    const std::string normalizedName = ToLowerAscii(Trim(widgetTypeName));
    const auto widgetClass = FindDashboardWidgetClass(normalizedName);
    if (!widgetClass.has_value()) {
        return std::nullopt;
    }

    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || widget.widget->Class() != *widgetClass) {
                continue;
            }
            return LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

bool DashboardRenderer::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    lastError_.clear();
    if (!InitializeGdiplus() || !LoadPanelIcons()) {
        return false;
    }
    if (!InitializeDirect2D()) {
        return false;
    }
    if (!CreateFonts()) {
        lastError_ = "renderer:font_create_failed";
        Shutdown();
        return false;
    }
    if (fonts_.title == nullptr || fonts_.big == nullptr || fonts_.value == nullptr || fonts_.label == nullptr ||
        fonts_.text == nullptr || fonts_.smallFont == nullptr || fonts_.footer == nullptr ||
        fonts_.clockTime == nullptr || fonts_.clockDate == nullptr) {
        lastError_ = "renderer:font_create_failed";
        Shutdown();
        return false;
    }
    if (!MeasureFonts() || !ResolveLayout()) {
        Shutdown();
        return false;
    }
    d2dFirstDrawWarmupPending_ = true;
    return true;
}

void DashboardRenderer::Shutdown() {
    InvalidateMetricSourceCache();
    DestroyFonts();
    fontHeights_ = {};
    resolvedLayout_ = {};
    parsedWidgetInfoCache_.clear();
    textWidthCache_.clear();
    staticEditableAnchorRegions_.clear();
    dynamicEditableAnchorRegions_.clear();
    dynamicAnchorRegistrationEnabled_ = false;
    d2dFirstDrawWarmupPending_ = false;
    ClearD2DCaches();
    ReleasePanelIcons();
    ShutdownDirect2D();
    ShutdownGdiplus();
}

const DashboardMetricSource& DashboardRenderer::ResolveMetrics(const SystemSnapshot& snapshot) {
    if (cachedMetricSource_ == nullptr || cachedMetricSnapshot_ != &snapshot ||
        cachedMetricSnapshotRevision_ != snapshot.revision) {
        cachedMetricSource_ = std::make_unique<DashboardMetricSource>(snapshot, config_.metricScales);
        cachedMetricSnapshot_ = &snapshot;
        cachedMetricSnapshotRevision_ = snapshot.revision;
    }
    return *cachedMetricSource_;
}

void DashboardRenderer::InvalidateMetricSourceCache() {
    cachedMetricSource_.reset();
    cachedMetricSnapshot_ = nullptr;
    cachedMetricSnapshotRevision_ = 0;
}

bool DashboardRenderer::InitializeDirect2D() {
    if (d2dFactory_ == nullptr) {
        const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || d2dFactory_ == nullptr) {
            lastError_ = "renderer:d2d_factory_failed hr=0x" + FormatHresult(hr);
            return false;
        }
    }
    if (dwriteFactory_ == nullptr) {
        const HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || dwriteFactory_ == nullptr) {
            lastError_ = "renderer:dwrite_factory_failed hr=0x" + FormatHresult(hr);
            return false;
        }
    }
    if (!InitializeWic()) {
        return false;
    }
    if (d2dSolidStrokeStyle_ == nullptr) {
        d2dFactory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(), nullptr, 0, d2dSolidStrokeStyle_.GetAddressOf());
    }
    if (d2dDashedStrokeStyle_ == nullptr) {
        const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND,
            10.0f,
            D2D1_DASH_STYLE_DOT,
            0.0f);
        d2dFactory_->CreateStrokeStyle(props, nullptr, 0, d2dDashedStrokeStyle_.GetAddressOf());
    }
    return true;
}

bool DashboardRenderer::InitializeWic() {
    if (wicFactory_ != nullptr) {
        return true;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        lastError_ = "renderer:wic_com_init_failed hr=0x" + FormatHresult(initHr);
        return false;
    }
    wicComInitialized_ = initHr == S_OK || initHr == S_FALSE;

    const HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || wicFactory_ == nullptr) {
        lastError_ = "renderer:wic_factory_failed hr=0x" + FormatHresult(hr);
        if (wicComInitialized_) {
            CoUninitialize();
            wicComInitialized_ = false;
        }
        return false;
    }
    return true;
}

void DashboardRenderer::ShutdownDirect2D() {
    d2dDrawActive_ = false;
    d2dClipDepth_ = 0;
    d2dActiveRenderTarget_ = nullptr;
    d2dCacheOwnerTarget_ = nullptr;
    ClearD2DCaches();
    d2dDashedStrokeStyle_.Reset();
    d2dSolidStrokeStyle_.Reset();
    d2dWindowRenderTarget_.Reset();
    wicFactory_.Reset();
    if (wicComInitialized_) {
        CoUninitialize();
        wicComInitialized_ = false;
    }
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
}

bool DashboardRenderer::EnsureWindowRenderTarget() {
    if (hwnd_ == nullptr || !InitializeDirect2D()) {
        return false;
    }

    const UINT width = static_cast<UINT>(std::max(1, WindowWidth()));
    const UINT height = static_cast<UINT>(std::max(1, WindowHeight()));
    if (d2dWindowRenderTarget_ == nullptr) {
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const HRESULT hr = d2dFactory_->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(width, height)),
            d2dWindowRenderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || d2dWindowRenderTarget_ == nullptr) {
            lastError_ = "renderer:d2d_hwnd_target_failed hr=0x" + FormatHresult(hr);
            return false;
        }
        d2dCacheOwnerTarget_ = nullptr;
        ClearD2DCaches();
        return true;
    }

    const D2D1_SIZE_U currentSize = d2dWindowRenderTarget_->GetPixelSize();
    if (currentSize.width == width && currentSize.height == height) {
        return true;
    }
    const HRESULT hr = d2dWindowRenderTarget_->Resize(D2D1::SizeU(width, height));
    if (FAILED(hr)) {
        d2dWindowRenderTarget_.Reset();
        d2dCacheOwnerTarget_ = nullptr;
        ClearD2DCaches();
        return EnsureWindowRenderTarget();
    }
    ClearD2DCaches();
    return true;
}

bool DashboardRenderer::BeginDirect2DDraw(ID2D1RenderTarget* target) {
    if (target == nullptr) {
        lastError_ = "renderer:d2d_target_missing";
        return false;
    }
    if (d2dFirstDrawWarmupPending_) {
        d2dFirstDrawWarmupPending_ = false;
        DestroyFonts();
        if (!CreateFonts() || !MeasureFonts() || !ResolveLayout()) {
            d2dFirstDrawWarmupPending_ = true;
            return false;
        }
    }
    if (d2dCacheOwnerTarget_ != target) {
        ClearD2DCaches();
        d2dCacheOwnerTarget_ = target;
    }
    lastError_.clear();
    d2dActiveRenderTarget_ = target;
    d2dActiveRenderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2dActiveRenderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    d2dActiveRenderTarget_->BeginDraw();
    d2dDrawActive_ = true;
    d2dClipDepth_ = 0;
    return true;
}

void DashboardRenderer::EndDirect2DDraw() {
    if (!d2dDrawActive_ || d2dActiveRenderTarget_ == nullptr) {
        return;
    }
    while (d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
    const bool activeWindowTarget = d2dActiveRenderTarget_ == d2dWindowRenderTarget_.Get();
    const HRESULT hr = d2dActiveRenderTarget_->EndDraw();
    d2dActiveRenderTarget_ = nullptr;
    d2dDrawActive_ = false;
    if (activeWindowTarget && hr == D2DERR_RECREATE_TARGET) {
        d2dWindowRenderTarget_.Reset();
        d2dCacheOwnerTarget_ = nullptr;
        ClearD2DCaches();
    } else if (FAILED(hr)) {
        lastError_ = "renderer:d2d_end_draw_failed hr=0x" + FormatHresult(hr);
    }
}

bool DashboardRenderer::BeginWindowDraw() {
    if (!EnsureWindowRenderTarget() || d2dWindowRenderTarget_ == nullptr) {
        return false;
    }
    return BeginDirect2DDraw(d2dWindowRenderTarget_.Get());
}

void DashboardRenderer::EndWindowDraw() {
    EndDirect2DDraw();
}

ID2D1SolidColorBrush* DashboardRenderer::D2DSolidBrush(COLORREF color, BYTE alpha) {
    if (d2dActiveRenderTarget_ == nullptr) {
        return nullptr;
    }
    const D2DBrushCacheKey key{color, alpha};
    if (const auto it = d2dSolidBrushCache_.find(key); it != d2dSolidBrushCache_.end()) {
        return it->second.Get();
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(d2dActiveRenderTarget_->CreateSolidColorBrush(ToD2DColor(color, alpha), brush.GetAddressOf())) ||
        brush == nullptr) {
        return nullptr;
    }
    return d2dSolidBrushCache_.emplace(key, std::move(brush)).first->second.Get();
}

void DashboardRenderer::PushClipRect(const RECT& rect) {
    if (IsDirect2DActive()) {
        d2dActiveRenderTarget_->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(rect.left),
                                                        static_cast<float>(rect.top),
                                                        static_cast<float>(rect.right),
                                                        static_cast<float>(rect.bottom)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++d2dClipDepth_;
    }
}

void DashboardRenderer::PopClipRect() {
    if (IsDirect2DActive() && d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
}

bool DashboardRenderer::FillSolidRect(const RECT& rect, COLORREF color, BYTE alpha) {
    if (!IsDirect2DActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color, alpha);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->FillRectangle(D2D1::RectF(static_cast<float>(rect.left),
                                              static_cast<float>(rect.top),
                                              static_cast<float>(rect.right),
                                              static_cast<float>(rect.bottom)),
        brush);
    return true;
}

bool DashboardRenderer::FillSolidEllipse(int centerX, int centerY, int diameter, COLORREF color, BYTE alpha) {
    if (!IsDirect2DActive() || diameter <= 0) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color, alpha);
    if (brush == nullptr) {
        return false;
    }
    const float radius = static_cast<float>(diameter) / 2.0f;
    d2dActiveRenderTarget_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(static_cast<float>(centerX), static_cast<float>(centerY)), radius, radius), brush);
    return true;
}

bool DashboardRenderer::FillSolidDiamond(const RECT& rect, COLORREF color, BYTE alpha) {
    if (!IsDirect2DActive()) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DPathGeometry();
    if (geometry == nullptr) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf())) || sink == nullptr) {
        return false;
    }
    const float left = static_cast<float>(rect.left);
    const float right = static_cast<float>(rect.right - 1);
    const float top = static_cast<float>(rect.top);
    const float bottom = static_cast<float>(rect.bottom - 1);
    const float centerX = left + (right - left) / 2.0f;
    const float centerY = top + (bottom - top) / 2.0f;
    sink->BeginFigure(D2D1::Point2F(centerX, top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(right, centerY));
    sink->AddLine(D2D1::Point2F(centerX, bottom));
    sink->AddLine(D2D1::Point2F(left, centerY));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        return false;
    }
    return FillD2DGeometry(geometry.Get(), color, alpha);
}

bool DashboardRenderer::DrawSolidRect(const RECT& rect, COLORREF color, int strokeWidth, bool dashed) {
    if (!IsDirect2DActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawRectangle(D2D1::RectF(static_cast<float>(rect.left),
                                              static_cast<float>(rect.top),
                                              static_cast<float>(rect.right),
                                              static_cast<float>(rect.bottom)),
        brush,
        static_cast<float>(std::max(1, strokeWidth)),
        dashed ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::DrawSolidEllipse(const RECT& rect, COLORREF color, int strokeWidth) {
    if (!IsDirect2DActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    const float radiusX = static_cast<float>(std::max(1L, rect.right - rect.left)) / 2.0f;
    const float radiusY = static_cast<float>(std::max(1L, rect.bottom - rect.top)) / 2.0f;
    d2dActiveRenderTarget_->DrawEllipse(
        D2D1::Ellipse(D2D1::Point2F(static_cast<float>(rect.left) + radiusX, static_cast<float>(rect.top) + radiusY),
            radiusX,
            radiusY),
        brush,
        static_cast<float>(std::max(1, strokeWidth)),
        d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::DrawSolidLine(POINT start, POINT end, COLORREF color, int strokeWidth) {
    if (!IsDirect2DActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawLine(D2D1::Point2F(static_cast<float>(start.x), static_cast<float>(start.y)),
        D2D1::Point2F(static_cast<float>(end.x), static_cast<float>(end.y)),
        brush,
        static_cast<float>(std::max(1, strokeWidth)),
        d2dSolidStrokeStyle_.Get());
    return true;
}

Microsoft::WRL::ComPtr<ID2D1PathGeometry> DashboardRenderer::CreateD2DPathGeometry() const {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (d2dFactory_ != nullptr) {
        d2dFactory_->CreatePathGeometry(geometry.GetAddressOf());
    }
    return geometry;
}

Microsoft::WRL::ComPtr<ID2D1GeometryGroup> DashboardRenderer::CreateD2DGeometryGroup(
    std::span<const Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometries, size_t count) const {
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> group;
    if (d2dFactory_ == nullptr || count == 0) {
        return group;
    }
    std::vector<ID2D1Geometry*> raw;
    raw.reserve(count);
    for (size_t i = 0; i < count && i < geometries.size(); ++i) {
        if (geometries[i] != nullptr) {
            raw.push_back(geometries[i].Get());
        }
    }
    if (!raw.empty()) {
        d2dFactory_->CreateGeometryGroup(
            D2D1_FILL_MODE_WINDING, raw.data(), static_cast<UINT32>(raw.size()), group.GetAddressOf());
    }
    return group;
}

bool DashboardRenderer::FillD2DGeometry(ID2D1Geometry* geometry, COLORREF color, BYTE alpha) {
    if (!IsDirect2DActive() || geometry == nullptr) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color, alpha);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->FillGeometry(geometry, brush);
    return true;
}

bool DashboardRenderer::DrawD2DPolyline(std::span<const POINT> points, COLORREF color, int strokeWidth) {
    if (!IsDirect2DActive() || points.size() < 2) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DPathGeometry();
    if (geometry == nullptr) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf())) || sink == nullptr) {
        return false;
    }
    sink->BeginFigure(D2D1::Point2F(static_cast<float>(points.front().x), static_cast<float>(points.front().y)),
        D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(D2D1::Point2F(static_cast<float>(points[i].x), static_cast<float>(points[i].y)));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawGeometry(
        geometry.Get(), brush, static_cast<float>(std::max(1, strokeWidth)), d2dSolidStrokeStyle_.Get());
    return true;
}

IDWriteTextFormat* DashboardRenderer::DWriteTextFormatForFont(HFONT font) const {
    if (font == fonts_.title)
        return d2dTextFormats_.title.Get();
    if (font == fonts_.big)
        return d2dTextFormats_.big.Get();
    if (font == fonts_.value)
        return d2dTextFormats_.value.Get();
    if (font == fonts_.label)
        return d2dTextFormats_.label.Get();
    if (font == fonts_.text)
        return d2dTextFormats_.text.Get();
    if (font == fonts_.smallFont)
        return d2dTextFormats_.smallFont.Get();
    if (font == fonts_.footer)
        return d2dTextFormats_.footer.Get();
    if (font == fonts_.clockTime)
        return d2dTextFormats_.clockTime.Get();
    if (font == fonts_.clockDate)
        return d2dTextFormats_.clockDate.Get();
    return nullptr;
}

bool DashboardRenderer::CreateDWriteTextFormats() {
    d2dTextFormats_ = {};
    if (dwriteFactory_ == nullptr) {
        return true;
    }

    const auto createFormat = [&](const UiFontConfig& fontConfig, Microsoft::WRL::ComPtr<IDWriteTextFormat>& format) {
        const std::wstring face = WideFromUtf8(fontConfig.face);
        const HRESULT hr = dwriteFactory_->CreateTextFormat(face.c_str(),
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(fontConfig.weight),
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(fontConfig.size),
            L"en-us",
            format.ReleaseAndGetAddressOf());
        return SUCCEEDED(hr) && format != nullptr;
    };

    UiFontConfig titleFont = config_.layout.fonts.title;
    UiFontConfig bigFont = config_.layout.fonts.big;
    UiFontConfig valueFont = config_.layout.fonts.value;
    UiFontConfig labelFont = config_.layout.fonts.label;
    UiFontConfig textFont = config_.layout.fonts.text;
    UiFontConfig smallFont = config_.layout.fonts.smallText;
    UiFontConfig footerFont = config_.layout.fonts.footer;
    UiFontConfig clockTimeFont = config_.layout.fonts.clockTime;
    UiFontConfig clockDateFont = config_.layout.fonts.clockDate;
    titleFont.size = ScaleLogical(titleFont.size);
    bigFont.size = ScaleLogical(bigFont.size);
    valueFont.size = ScaleLogical(valueFont.size);
    labelFont.size = ScaleLogical(labelFont.size);
    textFont.size = ScaleLogical(textFont.size);
    smallFont.size = ScaleLogical(smallFont.size);
    footerFont.size = ScaleLogical(footerFont.size);
    clockTimeFont.size = ScaleLogical(clockTimeFont.size);
    clockDateFont.size = ScaleLogical(clockDateFont.size);

    return createFormat(titleFont, d2dTextFormats_.title) && createFormat(bigFont, d2dTextFormats_.big) &&
           createFormat(valueFont, d2dTextFormats_.value) && createFormat(labelFont, d2dTextFormats_.label) &&
           createFormat(textFont, d2dTextFormats_.text) && createFormat(smallFont, d2dTextFormats_.smallFont) &&
           createFormat(footerFont, d2dTextFormats_.footer) && createFormat(clockTimeFont, d2dTextFormats_.clockTime) &&
           createFormat(clockDateFont, d2dTextFormats_.clockDate);
}

void DashboardRenderer::ConfigureDWriteTextFormat(IDWriteTextFormat* format, UINT drawTextFormat) const {
    if (format == nullptr) {
        return;
    }
    format->SetTextAlignment(DWriteTextAlignment(drawTextFormat));
    format->SetParagraphAlignment(DWriteParagraphAlignment(drawTextFormat));
    format->SetWordWrapping(((drawTextFormat & DT_SINGLELINE) != 0 || (drawTextFormat & DT_END_ELLIPSIS) != 0)
                                ? DWRITE_WORD_WRAPPING_NO_WRAP
                                : DWRITE_WORD_WRAPPING_WRAP);
}

DashboardRenderer::TextLayoutResult DashboardRenderer::MeasureTextBlockD2D(const RECT& rect,
    const std::wstring& wideText,
    HFONT font,
    UINT format,
    Microsoft::WRL::ComPtr<IDWriteTextLayout>* layoutOut) const {
    TextLayoutResult result{rect};
    IDWriteTextFormat* textFormat = DWriteTextFormatForFont(font);
    if (dwriteFactory_ == nullptr || textFormat == nullptr || wideText.empty()) {
        return result;
    }
    ConfigureDWriteTextFormat(textFormat, format);

    const float layoutWidth = static_cast<float>(std::max(1L, rect.right - rect.left));
    const float layoutHeight = static_cast<float>(std::max(1L, rect.bottom - rect.top));
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory_->CreateTextLayout(
            wideText.c_str(), static_cast<UINT32>(wideText.size()), textFormat, layoutWidth, layoutHeight, &layout)) ||
        layout == nullptr) {
        return result;
    }

    if ((format & DT_END_ELLIPSIS) != 0) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(textFormat, ellipsis.GetAddressOf()))) {
            layout->SetTrimming(&trimming, ellipsis.Get());
        }
    }

    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return result;
    }

    const int left = rect.left + static_cast<int>(std::lround(metrics.left));
    const int top = rect.top + static_cast<int>(std::lround(metrics.top));
    const int width = static_cast<int>(std::lround(metrics.widthIncludingTrailingWhitespace));
    const int height = static_cast<int>(std::lround(metrics.height));
    result.textRect = RECT{left, top, left + width, top + height};
    if (layoutOut != nullptr) {
        *layoutOut = std::move(layout);
    }
    return result;
}

bool DashboardRenderer::InitializeGdiplus() {
    if (gdiplusToken_ != 0) {
        return true;
    }
    Gdiplus::GdiplusStartupInput startupInput;
    const Gdiplus::Status status = Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr);
    if (status != Gdiplus::Ok) {
        lastError_ = "renderer:gdiplus_startup_failed status=" + std::to_string(static_cast<int>(status));
        return false;
    }
    return true;
}

void DashboardRenderer::ShutdownGdiplus() {
    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

bool DashboardRenderer::CreateFonts() {
    if (fonts_.title != nullptr && fonts_.big != nullptr && fonts_.value != nullptr && fonts_.label != nullptr &&
        fonts_.text != nullptr && fonts_.smallFont != nullptr && fonts_.footer != nullptr &&
        fonts_.clockTime != nullptr && fonts_.clockDate != nullptr) {
        return true;
    }

    DestroyFonts();
    UiFontConfig titleFont = config_.layout.fonts.title;
    UiFontConfig bigFont = config_.layout.fonts.big;
    UiFontConfig valueFont = config_.layout.fonts.value;
    UiFontConfig labelFont = config_.layout.fonts.label;
    UiFontConfig textFont = config_.layout.fonts.text;
    UiFontConfig smallFont = config_.layout.fonts.smallText;
    UiFontConfig footerFont = config_.layout.fonts.footer;
    UiFontConfig clockTimeFont = config_.layout.fonts.clockTime;
    UiFontConfig clockDateFont = config_.layout.fonts.clockDate;
    titleFont.size = ScaleLogical(titleFont.size);
    bigFont.size = ScaleLogical(bigFont.size);
    valueFont.size = ScaleLogical(valueFont.size);
    labelFont.size = ScaleLogical(labelFont.size);
    textFont.size = ScaleLogical(textFont.size);
    smallFont.size = ScaleLogical(smallFont.size);
    footerFont.size = ScaleLogical(footerFont.size);
    clockTimeFont.size = ScaleLogical(clockTimeFont.size);
    clockDateFont.size = ScaleLogical(clockDateFont.size);
    fonts_.title = CreateUiFont(titleFont);
    fonts_.big = CreateUiFont(bigFont);
    fonts_.value = CreateUiFont(valueFont);
    fonts_.label = CreateUiFont(labelFont);
    fonts_.text = CreateUiFont(textFont);
    fonts_.smallFont = CreateUiFont(smallFont);
    fonts_.footer = CreateUiFont(footerFont);
    fonts_.clockTime = CreateUiFont(clockTimeFont);
    fonts_.clockDate = CreateUiFont(clockDateFont);
    const bool fontsCreated = fonts_.title != nullptr && fonts_.big != nullptr && fonts_.value != nullptr &&
                              fonts_.label != nullptr && fonts_.text != nullptr && fonts_.smallFont != nullptr &&
                              fonts_.footer != nullptr && fonts_.clockTime != nullptr && fonts_.clockDate != nullptr;
    if (!fontsCreated) {
        return false;
    }
    return CreateDWriteTextFormats();
}

void DashboardRenderer::DestroyFonts() {
    DeleteObject(fonts_.title);
    DeleteObject(fonts_.big);
    DeleteObject(fonts_.value);
    DeleteObject(fonts_.label);
    DeleteObject(fonts_.text);
    DeleteObject(fonts_.smallFont);
    DeleteObject(fonts_.footer);
    DeleteObject(fonts_.clockTime);
    DeleteObject(fonts_.clockDate);
    fonts_ = {};
    d2dTextFormats_ = {};
    textWidthCache_.clear();
}

void DashboardRenderer::ClearD2DCaches() {
    d2dSolidBrushCache_.clear();
    d2dPanelIconCache_.clear();
}

bool DashboardRenderer::LoadPanelIcons() {
    ReleasePanelIcons();
    std::set<std::string> uniqueIcons;
    for (const auto& card : config_.layout.cards) {
        if (!card.icon.empty()) {
            uniqueIcons.insert(card.icon);
        }
    }
    for (const auto& iconName : uniqueIcons) {
        const UINT resourceId = GetPanelIconResourceId(iconName);
        if (resourceId == 0) {
            lastError_ = "renderer:icon_unknown name=\"" + iconName + "\"";
            ReleasePanelIcons();
            return false;
        }
        auto bitmap = LoadPngResourceBitmap(resourceId);
        if (bitmap == nullptr) {
            lastError_ = "renderer:icon_load_failed name=\"" + iconName + "\" resource=" + std::to_string(resourceId);
            ReleasePanelIcons();
            return false;
        }
        panelIcons_.push_back({iconName, std::move(bitmap)});
    }
    return true;
}

void DashboardRenderer::ReleasePanelIcons() {
    d2dPanelIconCache_.clear();
    panelIcons_.clear();
}

void DashboardRenderer::WriteTrace(const std::string& text) const {
    if (traceOutput_ == nullptr) {
        return;
    }
    if (interactiveDragTraceActive_ && text.rfind("renderer:", 0) == 0) {
        return;
    }
    tracing::Trace trace(traceOutput_);
    trace.Write(text);
}

bool DashboardRenderer::MeasureFonts() {
    HDC hdc = GetDC(hwnd_ != nullptr ? hwnd_ : nullptr);
    if (hdc == nullptr) {
        lastError_ = "renderer:measure_fonts_failed " + FormatWin32Error(GetLastError());
        return false;
    }
    const auto measure = [&](HFONT font) {
        TEXTMETRICW metrics{};
        HGDIOBJ oldFont = SelectObject(hdc, font);
        GetTextMetricsW(hdc, &metrics);
        SelectObject(hdc, oldFont);
        return static_cast<int>(metrics.tmHeight);
    };
    fontHeights_.title = measure(fonts_.title);
    fontHeights_.big = measure(fonts_.big);
    fontHeights_.value = measure(fonts_.value);
    fontHeights_.label = measure(fonts_.label);
    fontHeights_.text = measure(fonts_.text);
    fontHeights_.smallText = measure(fonts_.smallFont);
    fontHeights_.footer = measure(fonts_.footer);
    fontHeights_.clockTime = measure(fonts_.clockTime);
    fontHeights_.clockDate = measure(fonts_.clockDate);
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    WriteTrace("renderer:font_metrics title=" + std::to_string(fontHeights_.title) +
               " big=" + std::to_string(fontHeights_.big) + " value=" + std::to_string(fontHeights_.value) +
               " label=" + std::to_string(fontHeights_.label) + " text=" + std::to_string(fontHeights_.text) +
               " small=" + std::to_string(fontHeights_.smallText) + " footer=" + std::to_string(fontHeights_.footer) +
               " clock_time=" + std::to_string(fontHeights_.clockTime) + " clock_date=" +
               std::to_string(fontHeights_.clockDate) + " render_scale=" + std::to_string(renderScale_));
    return true;
}

int DashboardRenderer::EffectiveHeaderHeight() const {
    const int titleHeight = std::max(fontHeights_.title, ScaleLogical(config_.layout.cardStyle.headerIconSize));
    const int configured = ScaleLogical(config_.layout.cardStyle.headerHeight);
    const int computed = std::max(configured, titleHeight);
    WriteTrace("renderer:layout_header_height configured=" + std::to_string(configured) +
               " title_or_icon=" + std::to_string(titleHeight) + " effective=" + std::to_string(computed));
    return computed;
}

bool DashboardRenderer::SupportsLayoutSimilarityIndicator(const DashboardWidgetLayout& widget) const {
    if (widget.widget == nullptr || widget.widget->IsVerticalSpring()) {
        return false;
    }
    if (UsesFixedPreferredHeightInRows(widget)) {
        return false;
    }
    return true;
}

std::vector<const DashboardWidgetLayout*> DashboardRenderer::CollectSimilarityIndicatorWidgets(
    LayoutGuideAxis axis) const {
    struct SimilarityRepresentativeKey {
        std::string cardId;
        DashboardWidgetClass widgetClass = DashboardWidgetClass::Unknown;
        int extent = 0;
        int edgeStart = 0;
        int edgeEnd = 0;

        bool operator==(const SimilarityRepresentativeKey& other) const {
            return cardId == other.cardId && widgetClass == other.widgetClass && extent == other.extent &&
                   edgeStart == other.edgeStart && edgeEnd == other.edgeEnd;
        }
    };

    struct SimilarityRepresentativeKeyHash {
        size_t operator()(const SimilarityRepresentativeKey& key) const {
            size_t hash = std::hash<std::string>{}(key.cardId);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(static_cast<int>(key.widgetClass));
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.extent);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.edgeStart);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.edgeEnd);
            return hash;
        }
    };

    std::vector<const DashboardWidgetLayout*> widgets;
    std::unordered_set<SimilarityRepresentativeKey, SimilarityRepresentativeKeyHash> seenKeys;
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (!SupportsLayoutSimilarityIndicator(widget) || widget.widget == nullptr) {
                continue;
            }

            const int extent = WidgetExtentForAxis(widget, axis);
            if (extent <= 0) {
                continue;
            }

            SimilarityRepresentativeKey key;
            key.cardId = widget.cardId;
            key.widgetClass = widget.widget->Class();
            key.extent = extent;
            if (axis == LayoutGuideAxis::Vertical) {
                key.edgeStart = widget.rect.left;
                key.edgeEnd = widget.rect.right;
            } else {
                key.edgeStart = widget.rect.top;
                key.edgeEnd = widget.rect.bottom;
            }
            if (!seenKeys.insert(std::move(key)).second) {
                continue;
            }
            widgets.push_back(&widget);
        }
    }
    return widgets;
}

int DashboardRenderer::PreferredNodeHeight(const LayoutNodeConfig& node, int) const {
    if (node.name == "rows") {
        int total = 0;
        for (size_t i = 0; i < node.children.size(); ++i) {
            total += PreferredNodeHeight(node.children[i], 0);
            if (i + 1 < node.children.size()) {
                total += ScaleLogical(config_.layout.cardStyle.widgetLineGap);
            }
        }
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(total));
        return total;
    }
    if (node.name == "columns") {
        int tallest = 0;
        for (const auto& child : node.children) {
            tallest = std::max(tallest, PreferredNodeHeight(child, 0));
        }
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(tallest));
        return tallest;
    }
    const ParsedWidgetInfo* widget = FindParsedWidgetInfo(node);
    const int preferredHeight = widget != nullptr ? widget->preferredHeight : 0;
    WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(preferredHeight));
    return preferredHeight;
}

bool DashboardRenderer::IsContainerNode(const LayoutNodeConfig& node) {
    return node.name == "rows" || node.name == "columns";
}

const DashboardRenderer::ParsedWidgetInfo* DashboardRenderer::FindParsedWidgetInfo(const LayoutNodeConfig& node) const {
    if (IsContainerNode(node)) {
        return nullptr;
    }

    const auto it = parsedWidgetInfoCache_.find(&node);
    if (it != parsedWidgetInfoCache_.end()) {
        return &it->second;
    }

    if (!FindDashboardWidgetClass(node.name).has_value()) {
        return nullptr;
    }

    auto widget = CreateDashboardWidget(node.name);
    if (widget == nullptr) {
        return nullptr;
    }

    widget->Initialize(node);
    ParsedWidgetInfo info;
    info.preferredHeight = widget->PreferredHeight(*this);
    info.fixedPreferredHeightInRows = widget->UsesFixedPreferredHeightInRows();
    info.verticalSpring = widget->IsVerticalSpring();
    info.widgetPrototype = std::move(widget);
    return &parsedWidgetInfoCache_.emplace(&node, std::move(info)).first->second;
}

DashboardWidgetLayout DashboardRenderer::ResolveWidgetLayout(
    const LayoutNodeConfig& node, const RECT& rect, bool instantiateWidget) const {
    DashboardWidgetLayout widget;
    widget.rect = rect;
    const ParsedWidgetInfo* info = FindParsedWidgetInfo(node);
    if (instantiateWidget && info != nullptr) {
        widget.widget = info->widgetPrototype->Clone();
    }
    return widget;
}

bool DashboardRenderer::UsesFixedPreferredHeightInRows(const DashboardWidgetLayout& widget) const {
    return widget.widget != nullptr && widget.widget->UsesFixedPreferredHeightInRows();
}

const LayoutCardConfig* DashboardRenderer::FindCardConfigById(const std::string& id) const {
    const auto it = std::find_if(
        config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) { return card.id == id; });
    return it != config_.layout.cards.end() ? &(*it) : nullptr;
}
