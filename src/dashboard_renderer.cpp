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
#include <wincodec.h>

#include "../resources/resource.h"
#include "trace.h"
#include "utf8.h"

namespace {

RenderColor ToRenderColor(unsigned int color, std::uint8_t alpha = 255) {
    return RenderColor{static_cast<std::uint8_t>((color >> 16) & 0xFFu),
        static_cast<std::uint8_t>((color >> 8) & 0xFFu),
        static_cast<std::uint8_t>(color & 0xFFu),
        alpha};
}

std::size_t TextStyleSlot(TextStyleId style) {
    return static_cast<std::size_t>(style);
}

std::string FormatHresult(HRESULT hr) {
    char buffer[16];
    sprintf_s(buffer, "%08X", static_cast<unsigned int>(hr));
    return buffer;
}

DWRITE_TEXT_ALIGNMENT DWriteTextAlignment(const TextLayoutOptions& options) {
    switch (options.horizontalAlign) {
        case TextHorizontalAlign::Center:
            return DWRITE_TEXT_ALIGNMENT_CENTER;
        case TextHorizontalAlign::Trailing:
            return DWRITE_TEXT_ALIGNMENT_TRAILING;
        case TextHorizontalAlign::Leading:
        default:
            return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

DWRITE_PARAGRAPH_ALIGNMENT DWriteParagraphAlignment(const TextLayoutOptions& options) {
    switch (options.verticalAlign) {
        case TextVerticalAlign::Center:
            return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        case TextVerticalAlign::Bottom:
            return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
        case TextVerticalAlign::Top:
        default:
            return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    }
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

UiFontConfig FontConfigForStyle(const UiFontSetConfig& fonts, TextStyleId style) {
    switch (style) {
        case TextStyleId::Title:
            return fonts.title;
        case TextStyleId::Big:
            return fonts.big;
        case TextStyleId::Value:
            return fonts.value;
        case TextStyleId::Label:
            return fonts.label;
        case TextStyleId::Text:
            return fonts.text;
        case TextStyleId::Small:
            return fonts.smallText;
        case TextStyleId::Footer:
            return fonts.footer;
        case TextStyleId::ClockTime:
            return fonts.clockTime;
        case TextStyleId::ClockDate:
            return fonts.clockDate;
    }
    return fonts.text;
}

Microsoft::WRL::ComPtr<IWICBitmapSource> LoadPngResourceBitmap(IWICImagingFactory* wicFactory, UINT resourceId) {
    Microsoft::WRL::ComPtr<IWICBitmapSource> bitmapSource;
    if (wicFactory == nullptr) {
        return bitmapSource;
    }

    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return bitmapSource;
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), L"PNG");
    if (resource == nullptr) {
        return bitmapSource;
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    HGLOBAL loadedResource = LoadResource(module, resource);
    if (loadedResource == nullptr || resourceSize == 0) {
        return bitmapSource;
    }

    const void* resourceData = LockResource(loadedResource);
    if (resourceData == nullptr) {
        return bitmapSource;
    }

    HGLOBAL copyHandle = GlobalAlloc(GMEM_MOVEABLE, resourceSize);
    if (copyHandle == nullptr) {
        return bitmapSource;
    }

    void* copyData = GlobalLock(copyHandle);
    if (copyData == nullptr) {
        GlobalFree(copyHandle);
        return bitmapSource;
    }

    memcpy(copyData, resourceData, resourceSize);
    GlobalUnlock(copyHandle);

    Microsoft::WRL::ComPtr<IStream> stream;
    if (CreateStreamOnHGlobal(copyHandle, TRUE, stream.GetAddressOf()) != S_OK || stream == nullptr) {
        GlobalFree(copyHandle);
        return bitmapSource;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr) || decoder == nullptr) {
        return bitmapSource;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr) || frame == nullptr) {
        return bitmapSource;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr) || converter == nullptr) {
        return bitmapSource;
    }

    hr = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return bitmapSource;
    }

    bitmapSource = converter;
    return bitmapSource;
}

}  // namespace

DashboardRenderer::DashboardRenderer() = default;

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

void DashboardRenderer::SetConfig(const AppConfig& config) {
    InvalidateMetricSourceCache();
    config_ = config;
    RebuildPalette();
    if (dwriteFactory_ != nullptr) {
        if (!RebuildTextFormatsAndMetrics() || !ResolveLayout()) {
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
    if (dwriteFactory_ != nullptr) {
        if (!RebuildTextFormatsAndMetrics() || !ResolveLayout()) {
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

const DashboardRenderer::TextStyleMetrics& DashboardRenderer::TextMetrics() const {
    return textStyleMetrics_;
}

DashboardRenderer::RenderMode DashboardRenderer::CurrentRenderMode() const {
    return renderMode_;
}

bool DashboardRenderer::IsDrawActive() const {
    return d2dActiveRenderTarget_ != nullptr;
}

RenderColor DashboardRenderer::TrackColor() const {
    return palette_.track;
}

std::vector<DashboardRenderer::WidgetEditGuide>& DashboardRenderer::WidgetEditGuidesMutable() {
    return widgetEditGuides_;
}

std::vector<DashboardRenderer::GapEditAnchor>& DashboardRenderer::GapEditAnchorsMutable() {
    return gapEditAnchors_;
}

int DashboardRenderer::WindowWidth() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.width));
}

int DashboardRenderer::WindowHeight() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.height));
}

RenderColor DashboardRenderer::BackgroundColor() const {
    return palette_.background;
}

RenderColor DashboardRenderer::ForegroundColor() const {
    return palette_.foreground;
}

RenderColor DashboardRenderer::AccentColor() const {
    return palette_.accent;
}

RenderColor DashboardRenderer::LayoutGuideColor() const {
    return palette_.layoutGuide;
}

RenderColor DashboardRenderer::ActiveEditColor() const {
    return palette_.activeEdit;
}

RenderColor DashboardRenderer::MutedTextColor() const {
    return palette_.mutedText;
}

RenderColor DashboardRenderer::GraphBackgroundColor() const {
    return palette_.graphBackground;
}

RenderColor DashboardRenderer::GraphMarkerColor() const {
    return palette_.graphMarker;
}

RenderColor DashboardRenderer::GraphAxisColor() const {
    return palette_.graphAxis;
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

const std::vector<DashboardRenderer::GapEditAnchor>& DashboardRenderer::GapEditAnchors() const {
    return gapEditAnchors_;
}

int DashboardRenderer::LayoutSimilarityThreshold() const {
    return std::max(0, ScaleLogical(config_.layout.layoutEditor.sizeSimilarityThreshold));
}

void DashboardRenderer::ResolveNodeWidgets(const LayoutNodeConfig& node,
    const RenderRect& rect,
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
    const RenderRect& rect,
    const std::vector<RenderRect>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    DashboardLayoutResolver::AddLayoutEditGuide(*this, node, rect, childRects, gap, renderCardId, editCardId, nodePath);
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
    const RenderRect& rect,
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
    const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const {
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return TextLayoutResult{rect};
    }
    return MeasureTextBlockD2D(rect, wideText, style, options, nullptr);
}

DashboardRenderer::TextLayoutResult DashboardRenderer::DrawTextBlock(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    RenderColor color,
    const TextLayoutOptions& options) {
    TextLayoutResult result{rect};
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return result;
    }
    result = MeasureTextBlockD2D(rect, wideText, style, options, nullptr);
    IDWriteTextFormat* textFormat = DWriteTextFormat(style);
    if (textFormat == nullptr) {
        return result;
    }
    ConfigureDWriteTextFormat(textFormat, options);
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return result;
    }
    const D2D1_DRAW_TEXT_OPTIONS drawOptions = options.clip ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE;
    d2dActiveRenderTarget_->DrawText(
        wideText.c_str(), static_cast<UINT32>(wideText.size()), textFormat, rect.ToD2DRectF(), brush, drawOptions);
    return result;
}

void DashboardRenderer::DrawText(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    RenderColor color,
    const TextLayoutOptions& options) const {
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return;
    }
    IDWriteTextFormat* textFormat = DWriteTextFormat(style);
    if (textFormat == nullptr) {
        return;
    }
    ConfigureDWriteTextFormat(textFormat, options);
    ID2D1SolidColorBrush* brush = const_cast<DashboardRenderer*>(this)->D2DSolidBrush(color);
    if (brush == nullptr) {
        return;
    }
    const D2D1_DRAW_TEXT_OPTIONS drawOptions = options.clip ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE;
    d2dActiveRenderTarget_->DrawText(
        wideText.c_str(), static_cast<UINT32>(wideText.size()), textFormat, rect.ToD2DRectF(), brush, drawOptions);
}

void DashboardRenderer::DrawHoveredWidgetHighlight(const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || !overlayState.hoveredEditableWidget.has_value() ||
        overlayState.hoveredEditableWidget->kind != LayoutWidgetIdentity::Kind::Widget) {
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
    const_cast<DashboardRenderer*>(this)->DrawSolidRect(hoveredWidget->rect, RenderStroke::Solid(LayoutGuideColor()));
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
                if (region.key.widget.kind != LayoutWidgetIdentity::Kind::Widget ||
                    region.key.widget.renderCardId != overlayState.hoveredEditableWidget->renderCardId ||
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
    if (overlayState.hoveredEditableCard.has_value()) {
        const auto collectHoveredCard = [&](const std::vector<EditableAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered || region.key.widget.kind != LayoutWidgetIdentity::Kind::CardChrome ||
                    region.key.widget.renderCardId != overlayState.hoveredEditableCard->renderCardId ||
                    region.key.widget.editCardId != overlayState.hoveredEditableCard->editCardId) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHoveredCard(staticEditableAnchorRegions_);
        collectHoveredCard(dynamicEditableAnchorRegions_);
    }
    appendByKey(overlayState.hoveredEditableAnchor, false);
    appendByKey(overlayState.activeEditableAnchor, true);
    if (highlights.empty()) {
        return;
    }
    for (const auto& [highlighted, active] : highlights) {
        const RenderColor outlineColor = active ? ActiveEditColor() : LayoutGuideColor();
        if (highlighted.drawTargetOutline && !highlighted.targetRect.IsEmpty()) {
            const RenderRect outlineRect =
                highlighted.targetRect.Inflate(std::max(1, ScaleLogical(1)), std::max(1, ScaleLogical(1)));
            const_cast<DashboardRenderer*>(this)->DrawSolidRect(
                outlineRect, RenderStroke::Dotted(outlineColor, static_cast<float>(std::max(1, ScaleLogical(1)))));
        }

        if (highlighted.shape == AnchorShape::Circle) {
            const_cast<DashboardRenderer*>(this)->DrawSolidEllipse(highlighted.anchorRect,
                RenderStroke::Solid(outlineColor, static_cast<float>(std::max(1, ScaleLogical(1)))));
        } else if (highlighted.shape == AnchorShape::Diamond) {
            const_cast<DashboardRenderer*>(this)->FillSolidDiamond(highlighted.anchorRect, outlineColor);
        } else {
            const_cast<DashboardRenderer*>(this)->FillSolidRect(highlighted.anchorRect, outlineColor);
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
        const RenderColor color = active ? ActiveEditColor() : LayoutGuideColor();
        const RenderPoint start{guide.lineRect.left, guide.lineRect.top};
        const RenderPoint end = guide.axis == LayoutGuideAxis::Vertical
                                    ? RenderPoint{guide.lineRect.left, guide.lineRect.bottom}
                                    : RenderPoint{guide.lineRect.right, guide.lineRect.top};
        const_cast<DashboardRenderer*>(this)->DrawSolidLine(start, end, RenderStroke::Solid(color));
    }
}

void DashboardRenderer::DrawWidgetEditGuides(const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || widgetEditGuides_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const WidgetEditGuide& guide) {
        if (overlayState.activeWidgetEditGuide.has_value()) {
            return guide.widget.kind == overlayState.activeWidgetEditGuide->widget.kind &&
                   guide.widget.renderCardId == overlayState.activeWidgetEditGuide->widget.renderCardId &&
                   guide.widget.editCardId == overlayState.activeWidgetEditGuide->widget.editCardId &&
                   guide.widget.nodePath == overlayState.activeWidgetEditGuide->widget.nodePath;
        }
        if (guide.widget.kind == LayoutWidgetIdentity::Kind::CardChrome) {
            return overlayState.hoveredEditableCard.has_value() &&
                   guide.widget.renderCardId == overlayState.hoveredEditableCard->renderCardId &&
                   guide.widget.editCardId == overlayState.hoveredEditableCard->editCardId;
        }
        if (!overlayState.hoveredEditableWidget.has_value()) {
            return false;
        }
        return guide.widget.kind == LayoutWidgetIdentity::Kind::Widget &&
               guide.widget.renderCardId == overlayState.hoveredEditableWidget->renderCardId &&
               guide.widget.editCardId == overlayState.hoveredEditableWidget->editCardId &&
               guide.widget.nodePath == overlayState.hoveredEditableWidget->nodePath;
    };

    for (const auto& guide : widgetEditGuides_) {
        if (!shouldDraw(guide)) {
            continue;
        }
        const bool active = overlayState.activeWidgetEditGuide.has_value() &&
                            MatchesWidgetEditGuide(guide, *overlayState.activeWidgetEditGuide);
        const RenderColor color = active ? ActiveEditColor() : LayoutGuideColor();
        const_cast<DashboardRenderer*>(this)->DrawSolidLine(guide.drawStart, guide.drawEnd, RenderStroke::Solid(color));
    }
}

void DashboardRenderer::DrawGapEditAnchors(const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || gapEditAnchors_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const GapEditAnchor& anchor) {
        if (overlayState.activeGapEditAnchor.has_value()) {
            if (anchor.key.widget.kind == LayoutWidgetIdentity::Kind::DashboardChrome) {
                return overlayState.activeGapEditAnchor->widget.kind == LayoutWidgetIdentity::Kind::DashboardChrome;
            }
            return overlayState.activeGapEditAnchor->widget.kind == LayoutWidgetIdentity::Kind::CardChrome &&
                   anchor.key.widget.renderCardId == overlayState.activeGapEditAnchor->widget.renderCardId &&
                   anchor.key.widget.editCardId == overlayState.activeGapEditAnchor->widget.editCardId;
        }
        if (anchor.key.widget.kind == LayoutWidgetIdentity::Kind::DashboardChrome) {
            return !overlayState.hoveredLayoutCard.has_value();
        }
        return overlayState.hoveredLayoutCard.has_value() &&
               anchor.key.widget.renderCardId == overlayState.hoveredLayoutCard->renderCardId &&
               anchor.key.widget.editCardId == overlayState.hoveredLayoutCard->editCardId;
    };

    const int capHalf = (std::max)(2, ScaleLogical(4));
    const int lineWidth = (std::max)(1, ScaleLogical(1));
    const int handleOutline = (std::max)(1, ScaleLogical(1));
    for (const auto& anchor : gapEditAnchors_) {
        if (!shouldDraw(anchor)) {
            continue;
        }
        const bool active = overlayState.activeGapEditAnchor.has_value() &&
                            MatchesGapEditAnchorKey(anchor.key, *overlayState.activeGapEditAnchor);
        const bool hovered = overlayState.hoveredGapEditAnchor.has_value() &&
                             MatchesGapEditAnchorKey(anchor.key, *overlayState.hoveredGapEditAnchor);
        const RenderColor color = active ? ActiveEditColor() : LayoutGuideColor();

        const_cast<DashboardRenderer*>(this)->DrawSolidLine(
            anchor.drawStart, anchor.drawEnd, RenderStroke::Solid(color, static_cast<float>(lineWidth)));
        if (anchor.axis == LayoutGuideAxis::Vertical) {
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawStart.x - capHalf, anchor.drawStart.y},
                RenderPoint{anchor.drawStart.x + capHalf, anchor.drawStart.y},
                RenderStroke::Solid(color, static_cast<float>(lineWidth)));
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawEnd.x - capHalf, anchor.drawEnd.y},
                RenderPoint{anchor.drawEnd.x + capHalf, anchor.drawEnd.y},
                RenderStroke::Solid(color, static_cast<float>(lineWidth)));
        } else {
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawStart.x, anchor.drawStart.y - capHalf},
                RenderPoint{anchor.drawStart.x, anchor.drawStart.y + capHalf},
                RenderStroke::Solid(color, static_cast<float>(lineWidth)));
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawEnd.x, anchor.drawEnd.y - capHalf},
                RenderPoint{anchor.drawEnd.x, anchor.drawEnd.y + capHalf},
                RenderStroke::Solid(color, static_cast<float>(lineWidth)));
        }

        if (active || hovered) {
            const_cast<DashboardRenderer*>(this)->FillSolidRect(anchor.handleRect, color);
        } else {
            const_cast<DashboardRenderer*>(this)->DrawSolidRect(
                anchor.handleRect, RenderStroke::Solid(color, static_cast<float>(handleOutline)));
        }
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
    return identity.kind == LayoutWidgetIdentity::Kind::Widget && widget.cardId == identity.renderCardId &&
           widget.editCardId == identity.editCardId && widget.nodePath == identity.nodePath;
}

bool DashboardRenderer::MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) const {
    return left.axis == right.axis && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath && left.separatorIndex == right.separatorIndex;
}

bool DashboardRenderer::MatchesGapEditAnchorKey(const GapEditAnchorKey& left, const GapEditAnchorKey& right) const {
    return left.parameter == right.parameter && left.widget.kind == right.widget.kind &&
           left.widget.renderCardId == right.widget.renderCardId && left.widget.editCardId == right.widget.editCardId &&
           left.widget.nodePath == right.widget.nodePath && left.nodePath == right.nodePath;
}

bool DashboardRenderer::MatchesEditableAnchorKey(const EditableAnchorKey& left, const EditableAnchorKey& right) const {
    return left.parameter == right.parameter && left.anchorId == right.anchorId &&
           left.widget.kind == right.widget.kind && left.widget.renderCardId == right.widget.renderCardId &&
           left.widget.editCardId == right.widget.editCardId && left.widget.nodePath == right.widget.nodePath;
}

bool DashboardRenderer::MatchesWidgetEditGuide(const WidgetEditGuide& left, const WidgetEditGuide& right) const {
    return left.axis == right.axis && left.parameter == right.parameter && left.guideId == right.guideId &&
           left.widget.kind == right.widget.kind && left.widget.renderCardId == right.widget.renderCardId &&
           left.widget.editCardId == right.widget.editCardId && left.widget.nodePath == right.widget.nodePath;
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
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
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
    region.anchorHitRect = RenderRect{region.anchorRect.left - anchorHitInset,
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
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
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
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
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
    const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const EditableAnchorBinding& editable) {
    if (text.empty()) {
        return;
    }

    const TextLayoutResult result = MeasureTextBlock(rect, text, style, options);
    const int anchorSize = std::max(4, ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    const int anchorCenterX = result.textRect.right;
    const int anchorCenterY = result.textRect.top;
    const RenderRect anchorRect{anchorCenterX - anchorHalf,
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
        RenderPoint{anchorCenterX, anchorCenterY},
        1.0,
        false,
        true,
        editable.value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<EditableAnchorRegion>& regions,
    const TextLayoutResult& layoutResult,
    const EditableAnchorBinding& editable) {
    const RenderRect& textRect = layoutResult.textRect;
    if (textRect.right <= textRect.left || textRect.bottom <= textRect.top) {
        return;
    }

    const int anchorSize = std::max(4, ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    const int anchorCenterX = textRect.right;
    const int anchorCenterY = textRect.top;
    const RenderRect anchorRect{anchorCenterX - anchorHalf,
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
        RenderPoint{anchorCenterX, anchorCenterY},
        1.0,
        false,
        true,
        editable.value);
}

void DashboardRenderer::RegisterStaticTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const EditableAnchorBinding& editable) {
    RegisterTextAnchor(staticEditableAnchorRegions_, rect, text, style, options, editable);
}

void DashboardRenderer::RegisterDynamicTextAnchor(
    const TextLayoutResult& layoutResult, const EditableAnchorBinding& editable) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, layoutResult, editable);
}

void DashboardRenderer::RegisterDynamicTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const EditableAnchorBinding& editable) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, rect, text, style, options, editable);
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

    const RenderColor color = LayoutGuideColor();
    const int inset = std::max(2, ScaleLogical(4));
    const int cap = std::max(3, ScaleLogical(4));
    const int offset = std::max(4, ScaleLogical(6));
    const int notchDepth = std::max(3, ScaleLogical(4));
    const int notchSpacing = std::max(3, ScaleLogical(4));

    for (const SimilarityIndicator& indicator : indicators) {
        const RenderRect& rect = indicator.rect;
        if (indicator.axis == LayoutGuideAxis::Vertical) {
            const int y = rect.top + offset;
            const int left = rect.left + inset;
            const int right = rect.right - inset;
            const RenderStroke stroke = RenderStroke::Solid(color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(RenderPoint{left, y}, RenderPoint{right, y}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{left + cap, y - cap}, RenderPoint{left, y}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{left, y}, RenderPoint{left + cap, y + cap + 1}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{right - cap, y - cap}, RenderPoint{right, y}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{right, y}, RenderPoint{right - cap, y + cap + 1}, stroke);
            if (indicator.exactTypeOrdinal > 0) {
                const int cx = left + std::max(0, (right - left) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalWidth = (count - 1) * notchSpacing;
                int notchX = cx - (totalWidth / 2);
                for (int i = 0; i < count; ++i) {
                    const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                        RenderPoint{notchX, y - notchDepth}, RenderPoint{notchX, y + notchDepth + 1}, stroke);
                    notchX += notchSpacing;
                }
            }
        } else {
            const int x = rect.left + offset;
            const int top = rect.top + inset;
            const int bottom = rect.bottom - inset;
            const RenderStroke stroke = RenderStroke::Solid(color);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(RenderPoint{x, top}, RenderPoint{x, bottom}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{x - cap, top + cap}, RenderPoint{x, top}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{x, top}, RenderPoint{x + cap + 1, top + cap}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{x - cap, bottom - cap}, RenderPoint{x, bottom}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{x, bottom}, RenderPoint{x + cap + 1, bottom - cap}, stroke);
            if (indicator.exactTypeOrdinal > 0) {
                const int cy = top + std::max(0, (bottom - top) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalHeight = (count - 1) * notchSpacing;
                int notchY = cy - (totalHeight / 2);
                for (int i = 0; i < count; ++i) {
                    const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                        RenderPoint{x - notchDepth, notchY}, RenderPoint{x + notchDepth + 1, notchY}, stroke);
                    notchY += notchSpacing;
                }
            }
        }
    }
}

void DashboardRenderer::DrawMoveOverlay(const MoveOverlayState& overlayState) {
    if (!overlayState.visible || !IsDrawActive()) {
        return;
    }

    const int margin = ScaleLogical(16);
    const int padding = ScaleLogical(12);
    const int lineGap = ScaleLogical(6);
    const int cornerRadius = ScaleLogical(14);
    const float borderWidth = static_cast<float>((std::max)(1, ScaleLogical(1)));
    const int titleHeight = (std::max)(1, textStyleMetrics_.label);
    const int bodyHeight = (std::max)(1, textStyleMetrics_.smallText);

    char positionTextBuffer[96];
    sprintf_s(positionTextBuffer, "Pos: x=%d y=%d", overlayState.relativePosition.x, overlayState.relativePosition.y);
    char scaleTextBuffer[96];
    sprintf_s(scaleTextBuffer, "Scale: %.0f%% (%.2fx)", overlayState.monitorScale * 100.0, overlayState.monitorScale);

    const std::string titleText = "Move Mode";
    const std::string monitorText = "Monitor: " + overlayState.monitorName;
    const std::string positionText = positionTextBuffer;
    const std::string scaleText = scaleTextBuffer;
    const std::string hintText = "Left-click to place. Copy monitor name, scale, and x/y into config.";

    const int minContentWidth = ScaleLogical(220);
    const int maxContentWidth = (std::max)(minContentWidth, WindowWidth() - margin * 2 - padding * 2);
    int preferredContentWidth = minContentWidth;
    preferredContentWidth = (std::max)(preferredContentWidth, MeasureTextWidth(TextStyleId::Label, titleText));
    preferredContentWidth = (std::max)(preferredContentWidth, MeasureTextWidth(TextStyleId::Small, monitorText));
    preferredContentWidth = (std::max)(preferredContentWidth, MeasureTextWidth(TextStyleId::Small, positionText));
    preferredContentWidth = (std::max)(preferredContentWidth, MeasureTextWidth(TextStyleId::Small, scaleText));
    const int contentWidth = (std::min)(maxContentWidth, preferredContentWidth);
    const int hintHeight = MeasureTextBlock(
        RenderRect{0, 0, contentWidth, WindowHeight()}, hintText, TextStyleId::Small, TextLayoutOptions::Wrapped())
                               .textRect.Height();
    const int overlayWidth = contentWidth + padding * 2;
    const int overlayHeight = padding * 2 + titleHeight + lineGap + bodyHeight + lineGap + bodyHeight + lineGap +
                              bodyHeight + lineGap + hintHeight;
    const RenderRect overlayRect{margin, margin, margin + overlayWidth, margin + overlayHeight};

    ID2D1SolidColorBrush* fillBrush = D2DSolidBrush(BackgroundColor());
    ID2D1SolidColorBrush* borderBrush = D2DSolidBrush(AccentColor());
    const D2D1_ROUNDED_RECT roundedRect =
        D2D1::RoundedRect(overlayRect.ToD2DRectF(), static_cast<float>(cornerRadius), static_cast<float>(cornerRadius));
    if (fillBrush != nullptr) {
        d2dActiveRenderTarget_->FillRoundedRectangle(roundedRect, fillBrush);
    }
    if (borderBrush != nullptr) {
        d2dActiveRenderTarget_->DrawRoundedRectangle(roundedRect, borderBrush, borderWidth, d2dSolidStrokeStyle_.Get());
    }

    int y = overlayRect.top + padding;
    DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, y + titleHeight},
        titleText,
        TextStyleId::Label,
        AccentColor(),
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
    y += titleHeight + lineGap;

    const auto drawBodyLine = [&](const std::string& text, bool ellipsis = false) {
        DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, y + bodyHeight},
            text,
            TextStyleId::Small,
            ForegroundColor(),
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center, true, ellipsis));
        y += bodyHeight + lineGap;
    };
    drawBodyLine(monitorText, true);
    drawBodyLine(positionText);
    drawBodyLine(scaleText);
    DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, overlayRect.bottom - padding},
        hintText,
        TextStyleId::Small,
        MutedTextColor(),
        TextLayoutOptions::Wrapped());
}

void DashboardRenderer::DrawPanelIcon(const std::string& iconName, const RenderRect& iconRect) {
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
        if (wicFactory_ == nullptr) {
            return;
        }

        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        HRESULT hr = wicFactory_->CreateBitmapScaler(scaler.GetAddressOf());
        if (FAILED(hr) || scaler == nullptr) {
            return;
        }

        hr = scaler->Initialize(
            it->second.Get(), static_cast<UINT>(width), static_cast<UINT>(height), WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            return;
        }

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        hr = d2dActiveRenderTarget_->CreateBitmapFromWicBitmap(scaler.Get(), bitmap.GetAddressOf());
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
    const float radius = static_cast<float>(std::max(1, ScaleLogical(config_.layout.cardStyle.cardRadius)));
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<float>(card.rect.left),
                                                                static_cast<float>(card.rect.top),
                                                                static_cast<float>(card.rect.right),
                                                                static_cast<float>(card.rect.bottom)),
        radius,
        radius);
    ID2D1SolidColorBrush* fillBrush = D2DSolidBrush(palette_.panelFill);
    ID2D1SolidColorBrush* borderBrush = D2DSolidBrush(palette_.panelBorder);
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
        DrawText(card.titleRect,
            card.title,
            TextStyleId::Title,
            ForegroundColor(),
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
    }
}

void DashboardRenderer::DrawPillBar(
    const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    const RenderColor accentColor = AccentColor();
    const auto fillCapsule = [&](const RenderRect& capsuleRect, RenderColor color) {
        const int capsuleWidth = capsuleRect.Width();
        const int capsuleHeight = capsuleRect.Height();
        if (capsuleWidth <= 0 || capsuleHeight <= 0) {
            return;
        }
        ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
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

    fillCapsule(rect, TrackColor());

    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!drawFill) {
        return;
    }

    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);
    const int straightWidth = std::max(0, width - height);
    const int fillWidth = std::min(width, height + static_cast<int>(std::round(clampedRatio * straightWidth)));
    RenderRect fillRect = rect;
    fillRect.right = fillRect.left + fillWidth;
    fillCapsule(fillRect, accentColor);

    if (peakRatio.has_value()) {
        const double peak = std::clamp(*peakRatio, 0.0, 1.0);
        const int markerWidth = std::min(width, std::max(1, std::max(ScaleLogical(4), height)));
        const int centerX = rect.left + static_cast<int>(std::round(peak * width));
        const int minLeft = rect.left;
        const int maxLeft = rect.right - markerWidth;
        const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
        RenderRect markerRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
        fillCapsule(markerRect, accentColor.WithAlpha(96));
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
    d2dActiveRenderTarget_->Clear(BackgroundColor().ToD2DColorF());
    for (const auto& card : resolvedLayout_.cards) {
        DrawPanel(card);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidget(widget, metrics);
        }
    }
    DrawHoveredWidgetHighlight(overlayState);
    DrawHoveredEditableAnchorHighlight(overlayState);
    DrawLayoutEditGuides(overlayState);
    DrawGapEditAnchors(overlayState);
    DrawWidgetEditGuides(overlayState);
    DrawLayoutSimilarityIndicators(overlayState);
    DrawMoveOverlay(overlayState.moveOverlay);
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

int DashboardRenderer::MeasureTextWidth(TextStyleId style, std::string_view text) const {
    const TextWidthCacheLookupKey cacheKey{style, text};
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
    const RenderRect measureRect{0, 0, WindowWidth(), WindowHeight()};
    const int width = std::max(0,
        static_cast<int>(MeasureTextBlockD2D(measureRect,
            wideText,
            style,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
            nullptr)
                .textRect.right));
    textWidthCache_.emplace(TextWidthCacheKey{style, std::string(text)}, width);
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

std::optional<DashboardRenderer::LayoutWidgetIdentity> DashboardRenderer::HitTestLayoutCard(
    RenderPoint clientPoint) const {
    for (const auto& card : resolvedLayout_.cards) {
        if (card.rect.Contains(clientPoint)) {
            return LayoutWidgetIdentity{card.id, card.id, {}, LayoutWidgetIdentity::Kind::CardChrome};
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::LayoutWidgetIdentity> DashboardRenderer::HitTestEditableCard(
    RenderPoint clientPoint) const {
    for (const auto& card : resolvedLayout_.cards) {
        if (!card.rect.Contains(clientPoint) || clientPoint.y > card.contentRect.top) {
            continue;
        }
        return LayoutWidgetIdentity{card.id, card.id, {}, LayoutWidgetIdentity::Kind::CardChrome};
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::LayoutWidgetIdentity> DashboardRenderer::HitTestEditableWidget(
    RenderPoint clientPoint) const {
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || !widget.rect.Contains(clientPoint)) {
                continue;
            }
            return LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::GapEditAnchorKey> DashboardRenderer::HitTestGapEditAnchor(
    RenderPoint clientPoint) const {
    const GapEditAnchor* bestAnchor = nullptr;
    int bestPriority = 0;
    for (auto it = gapEditAnchors_.rbegin(); it != gapEditAnchors_.rend(); ++it) {
        if (!it->hitRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority(it->key.parameter);
        if (bestAnchor == nullptr || priority < bestPriority) {
            bestAnchor = &(*it);
            bestPriority = priority;
        }
    }
    return bestAnchor != nullptr ? std::optional<GapEditAnchorKey>(bestAnchor->key) : std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorKey> DashboardRenderer::HitTestEditableAnchorTarget(
    RenderPoint clientPoint) const {
    std::vector<const EditableAnchorRegion*> regions;
    regions.reserve(staticEditableAnchorRegions_.size() + dynamicEditableAnchorRegions_.size());
    for (const auto& region : staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if ((*it)->targetRect.Contains(clientPoint)) {
            return (*it)->key;
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorKey> DashboardRenderer::HitTestEditableAnchorHandle(
    RenderPoint clientPoint) const {
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
            const int width = std::max(1, region.anchorRect.right - region.anchorRect.left);
            const int height = std::max(1, region.anchorRect.bottom - region.anchorRect.top);
            const double radius = static_cast<double>(std::max(width, height)) / 2.0;
            const double centerX = static_cast<double>(region.anchorRect.left) + static_cast<double>(width) / 2.0;
            const double centerY = static_cast<double>(region.anchorRect.top) + static_cast<double>(height) / 2.0;
            const double dx = static_cast<double>(clientPoint.x) - centerX;
            const double dy = static_cast<double>(clientPoint.y) - centerY;
            const double distance = std::sqrt((dx * dx) + (dy * dy));
            hit = std::abs(distance - radius) <= static_cast<double>(region.anchorHitPadding);
        } else {
            hit = region.anchorHitRect.Contains(clientPoint);
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

std::optional<DashboardRenderer::GapEditAnchor> DashboardRenderer::FindGapEditAnchor(
    const GapEditAnchorKey& key) const {
    const auto it = std::find_if(gapEditAnchors_.begin(), gapEditAnchors_.end(), [&](const GapEditAnchor& anchor) {
        return MatchesGapEditAnchorKey(anchor.key, key);
    });
    if (it == gapEditAnchors_.end()) {
        return std::nullopt;
    }
    return *it;
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
    RebuildPalette();
    if (!InitializeDirect2D() || !LoadPanelIcons()) {
        return false;
    }
    if (!RebuildTextFormatsAndMetrics() || !ResolveLayout()) {
        Shutdown();
        return false;
    }
    d2dFirstDrawWarmupPending_ = true;
    return true;
}

void DashboardRenderer::Shutdown() {
    InvalidateMetricSourceCache();
    dwriteTextFormats_ = {};
    textStyleMetrics_ = {};
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
        if (!RebuildTextFormatsAndMetrics() || !ResolveLayout()) {
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
    d2dClipDepth_ = 0;
    return true;
}

void DashboardRenderer::EndDirect2DDraw() {
    if (d2dActiveRenderTarget_ == nullptr) {
        return;
    }
    while (d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
    const bool activeWindowTarget = d2dActiveRenderTarget_ == d2dWindowRenderTarget_.Get();
    const HRESULT hr = d2dActiveRenderTarget_->EndDraw();
    d2dActiveRenderTarget_ = nullptr;
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

ID2D1SolidColorBrush* DashboardRenderer::D2DSolidBrush(RenderColor color) {
    if (d2dActiveRenderTarget_ == nullptr) {
        return nullptr;
    }
    const D2DBrushCacheKey key{color};
    if (const auto it = d2dSolidBrushCache_.find(key); it != d2dSolidBrushCache_.end()) {
        return it->second.Get();
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(d2dActiveRenderTarget_->CreateSolidColorBrush(color.ToD2DColorF(), brush.GetAddressOf())) ||
        brush == nullptr) {
        return nullptr;
    }
    return d2dSolidBrushCache_.emplace(key, std::move(brush)).first->second.Get();
}

void DashboardRenderer::PushClipRect(const RenderRect& rect) {
    if (IsDrawActive()) {
        d2dActiveRenderTarget_->PushAxisAlignedClip(rect.ToD2DRectF(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++d2dClipDepth_;
    }
}

void DashboardRenderer::PopClipRect() {
    if (IsDrawActive() && d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
}

bool DashboardRenderer::FillSolidRect(const RenderRect& rect, RenderColor color) {
    if (!IsDrawActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->FillRectangle(rect.ToD2DRectF(), brush);
    return true;
}

bool DashboardRenderer::FillSolidEllipse(RenderPoint center, int diameter, RenderColor color) {
    if (!IsDrawActive() || diameter <= 0) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    const float radius = static_cast<float>(diameter) / 2.0f;
    d2dActiveRenderTarget_->FillEllipse(D2D1::Ellipse(center.ToD2DPoint2F(), radius, radius), brush);
    return true;
}

bool DashboardRenderer::FillSolidDiamond(const RenderRect& rect, RenderColor color) {
    if (!IsDrawActive()) {
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
    return FillD2DGeometry(geometry.Get(), color);
}

bool DashboardRenderer::DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke) {
    if (!IsDrawActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(stroke.color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawRectangle(rect.ToD2DRectF(),
        brush,
        (std::max)(1.0f, stroke.width),
        stroke.pattern == StrokePattern::Dotted ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke) {
    if (!IsDrawActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(stroke.color);
    if (brush == nullptr) {
        return false;
    }
    const float radiusX = static_cast<float>((std::max)(1, rect.Width())) / 2.0f;
    const float radiusY = static_cast<float>((std::max)(1, rect.Height())) / 2.0f;
    d2dActiveRenderTarget_->DrawEllipse(D2D1::Ellipse(rect.Center().ToD2DPoint2F(), radiusX, radiusY),
        brush,
        (std::max)(1.0f, stroke.width),
        d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke) {
    if (!IsDrawActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(stroke.color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawLine(start.ToD2DPoint2F(),
        end.ToD2DPoint2F(),
        brush,
        (std::max)(1.0f, stroke.width),
        stroke.pattern == StrokePattern::Dotted ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
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

bool DashboardRenderer::FillD2DGeometry(ID2D1Geometry* geometry, RenderColor color) {
    if (!IsDrawActive() || geometry == nullptr) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->FillGeometry(geometry, brush);
    return true;
}

bool DashboardRenderer::DrawD2DPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke) {
    if (!IsDrawActive() || points.size() < 2) {
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
    sink->BeginFigure(points.front().ToD2DPoint2F(), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(points[i].ToD2DPoint2F());
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(stroke.color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawGeometry(geometry.Get(),
        brush,
        (std::max)(1.0f, stroke.width),
        stroke.pattern == StrokePattern::Dotted ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
    return true;
}

IDWriteTextFormat* DashboardRenderer::DWriteTextFormat(TextStyleId style) const {
    return dwriteTextFormats_[TextStyleSlot(style)].Get();
}

bool DashboardRenderer::CreateDWriteTextFormats() {
    dwriteTextFormats_ = {};
    if (dwriteFactory_ == nullptr) {
        return true;
    }

    const auto createFormat = [&](TextStyleId style) {
        UiFontConfig fontConfig = FontConfigForStyle(config_.layout.fonts, style);
        fontConfig.size = ScaleLogical(fontConfig.size);
        const std::wstring face = WideFromUtf8(fontConfig.face);
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        const HRESULT hr = dwriteFactory_->CreateTextFormat(face.c_str(),
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(fontConfig.weight),
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(fontConfig.size),
            L"en-us",
            format.GetAddressOf());
        if (FAILED(hr) || format == nullptr) {
            return false;
        }
        dwriteTextFormats_[TextStyleSlot(style)] = std::move(format);
        return true;
    };

    return createFormat(TextStyleId::Title) && createFormat(TextStyleId::Big) && createFormat(TextStyleId::Value) &&
           createFormat(TextStyleId::Label) && createFormat(TextStyleId::Text) && createFormat(TextStyleId::Small) &&
           createFormat(TextStyleId::Footer) && createFormat(TextStyleId::ClockTime) &&
           createFormat(TextStyleId::ClockDate);
}

void DashboardRenderer::ConfigureDWriteTextFormat(IDWriteTextFormat* format, const TextLayoutOptions& options) const {
    if (format == nullptr) {
        return;
    }
    format->SetTextAlignment(DWriteTextAlignment(options));
    format->SetParagraphAlignment(DWriteParagraphAlignment(options));
    format->SetWordWrapping(options.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
}

DashboardRenderer::TextLayoutResult DashboardRenderer::MeasureTextBlockD2D(const RenderRect& rect,
    const std::wstring& wideText,
    TextStyleId style,
    const TextLayoutOptions& options,
    Microsoft::WRL::ComPtr<IDWriteTextLayout>* layoutOut) const {
    TextLayoutResult result{rect};
    IDWriteTextFormat* textFormat = DWriteTextFormat(style);
    if (dwriteFactory_ == nullptr || textFormat == nullptr || wideText.empty()) {
        return result;
    }
    ConfigureDWriteTextFormat(textFormat, options);

    const float layoutWidth = static_cast<float>((std::max)(1, rect.Width()));
    const float layoutHeight = static_cast<float>((std::max)(1, rect.Height()));
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory_->CreateTextLayout(
            wideText.c_str(), static_cast<UINT32>(wideText.size()), textFormat, layoutWidth, layoutHeight, &layout)) ||
        layout == nullptr) {
        return result;
    }

    if (options.ellipsis) {
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
    result.textRect = RenderRect{left, top, left + width, top + height};
    if (layoutOut != nullptr) {
        *layoutOut = std::move(layout);
    }
    return result;
}

void DashboardRenderer::RebuildPalette() {
    palette_.background = ToRenderColor(config_.layout.colors.backgroundColor);
    palette_.foreground = ToRenderColor(config_.layout.colors.foregroundColor);
    palette_.accent = ToRenderColor(config_.layout.colors.accentColor);
    palette_.mutedText = ToRenderColor(config_.layout.colors.mutedTextColor);
    palette_.track = ToRenderColor(config_.layout.colors.trackColor);
    palette_.layoutGuide = ToRenderColor(config_.layout.colors.layoutGuideColor);
    palette_.activeEdit = ToRenderColor(config_.layout.colors.activeEditColor);
    palette_.panelBorder = ToRenderColor(config_.layout.colors.panelBorderColor);
    palette_.panelFill = ToRenderColor(config_.layout.colors.panelFillColor);
    palette_.graphBackground = ToRenderColor(config_.layout.colors.graphBackgroundColor);
    palette_.graphMarker = ToRenderColor(config_.layout.colors.graphMarkerColor);
    palette_.graphAxis = ToRenderColor(config_.layout.colors.graphAxisColor);
}

void DashboardRenderer::ClearD2DCaches() {
    d2dSolidBrushCache_.clear();
    d2dPanelIconCache_.clear();
}

bool DashboardRenderer::LoadPanelIcons() {
    ReleasePanelIcons();
    if (wicFactory_ == nullptr && !InitializeWic()) {
        return false;
    }
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
        auto bitmap = LoadPngResourceBitmap(wicFactory_.Get(), resourceId);
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

bool DashboardRenderer::RebuildTextFormatsAndMetrics() {
    if (!CreateDWriteTextFormats()) {
        lastError_ = "renderer:text_format_create_failed";
        return false;
    }
    textWidthCache_.clear();
    textStyleMetrics_ = {};
    if (dwriteFactory_ == nullptr) {
        return true;
    }

    const auto measureStyle = [&](TextStyleId style) -> int {
        IDWriteTextFormat* format = DWriteTextFormat(style);
        if (format == nullptr) {
            return 0;
        }
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        const wchar_t sample[] = L"Ag";
        if (FAILED(dwriteFactory_->CreateTextLayout(
                sample, static_cast<UINT32>(std::size(sample) - 1), format, 1024.0f, 1024.0f, &layout)) ||
            layout == nullptr) {
            return 0;
        }
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) {
            return 0;
        }
        return (std::max)(1, static_cast<int>(std::lround(metrics.height)));
    };

    textStyleMetrics_.title = measureStyle(TextStyleId::Title);
    textStyleMetrics_.big = measureStyle(TextStyleId::Big);
    textStyleMetrics_.value = measureStyle(TextStyleId::Value);
    textStyleMetrics_.label = measureStyle(TextStyleId::Label);
    textStyleMetrics_.text = measureStyle(TextStyleId::Text);
    textStyleMetrics_.smallText = measureStyle(TextStyleId::Small);
    textStyleMetrics_.footer = measureStyle(TextStyleId::Footer);
    textStyleMetrics_.clockTime = measureStyle(TextStyleId::ClockTime);
    textStyleMetrics_.clockDate = measureStyle(TextStyleId::ClockDate);
    WriteTrace("renderer:text_metrics title=" + std::to_string(textStyleMetrics_.title) +
               " big=" + std::to_string(textStyleMetrics_.big) + " value=" + std::to_string(textStyleMetrics_.value) +
               " label=" + std::to_string(textStyleMetrics_.label) + " text=" + std::to_string(textStyleMetrics_.text) +
               " small=" + std::to_string(textStyleMetrics_.smallText) + " footer=" +
               std::to_string(textStyleMetrics_.footer) + " clock_time=" + std::to_string(textStyleMetrics_.clockTime) +
               " clock_date=" + std::to_string(textStyleMetrics_.clockDate) +
               " render_scale=" + std::to_string(renderScale_));
    return true;
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
                total += ScaleLogical(config_.layout.cardStyle.rowGap);
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
    const LayoutNodeConfig& node, const RenderRect& rect, bool instantiateWidget) const {
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
