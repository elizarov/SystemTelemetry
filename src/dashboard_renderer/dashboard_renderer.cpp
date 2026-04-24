#include "dashboard_renderer/dashboard_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <wincodec.h>

#include "dashboard_renderer/impl/d2d_cache.h"
#include "dashboard_renderer/impl/d2d_render_conversions.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "dashboard_renderer/impl/palette.h"
#include "dashboard_renderer/impl/text_width_cache.h"
#include "layout_edit/layout_edit_helpers.h"
#include "layout_edit/layout_edit_hit_priority.h"
#include "layout_edit/layout_edit_parameter_metadata.h"
#include "layout_edit/layout_edit_service.h"
#include "resource.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

std::size_t TextStyleSlot(TextStyleId style) {
    return static_cast<std::size_t>(style);
}

bool SameIconInputs(const std::vector<LayoutCardConfig>& left, const std::vector<LayoutCardConfig>& right) {
    std::set<std::string> leftIcons;
    for (const auto& card : left) {
        if (!card.icon.empty()) {
            leftIcons.insert(card.icon);
        }
    }

    std::set<std::string> rightIcons;
    for (const auto& card : right) {
        if (!card.icon.empty()) {
            rightIcons.insert(card.icon);
        }
    }

    return leftIcons == rightIcons;
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

std::string FormatTraceRect(const RenderRect& rect) {
    return "(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) + "," +
           std::to_string(rect.bottom) + ")";
}

std::string FormatNodePath(const std::vector<size_t>& nodePath) {
    if (nodePath.empty()) {
        return "root";
    }
    std::string text;
    for (size_t index : nodePath) {
        if (!text.empty()) {
            text += "/";
        }
        text += "children[";
        text += std::to_string(index);
        text += "]";
    }
    return text;
}

std::string ActiveLayoutSectionName(const AppConfig& config) {
    return config.display.layout.empty() ? "layout" : "layout." + config.display.layout;
}

std::string FormatLayoutConfigPath(
    const AppConfig& config, const std::string& editCardId, const std::vector<size_t>& nodePath) {
    if (editCardId.empty()) {
        return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(nodePath);
    }
    return "card." + editCardId + ".layout/" + FormatNodePath(nodePath);
}

std::string FormatLayoutEditParameterPath(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    if (!descriptor.has_value()) {
        return "parameter";
    }
    return descriptor->configKey;
}

std::string FormatLayoutEditParameterDetail(LayoutEditParameter parameter) {
    return GetLayoutEditParameterDisplayName(parameter) + " (" + FormatLayoutEditParameterPath(parameter) + ")";
}

std::string FormatWidgetIdentityPath(const AppConfig& config, const LayoutEditWidgetIdentity& widget) {
    switch (widget.kind) {
        case LayoutEditWidgetIdentity::Kind::DashboardChrome:
            return ActiveLayoutSectionName(config) + ".dashboard";
        case LayoutEditWidgetIdentity::Kind::CardChrome:
            return ActiveLayoutSectionName(config) + ".cards/card[" + widget.editCardId + "]";
        case LayoutEditWidgetIdentity::Kind::Widget:
        default:
            return FormatLayoutConfigPath(config, widget.editCardId, widget.nodePath);
    }
}

std::string FormatGuideAxis(LayoutGuideAxis axis) {
    return axis == LayoutGuideAxis::Vertical ? "vertical" : "horizontal";
}

std::string FormatAnchorShape(AnchorShape shape) {
    switch (shape) {
        case AnchorShape::Circle:
            return "circle";
        case AnchorShape::Diamond:
            return "diamond";
        case AnchorShape::Square:
            return "square";
        case AnchorShape::Wedge:
            return "wedge";
        case AnchorShape::VerticalReorder:
            return "vertical-reorder";
        case AnchorShape::HorizontalReorder:
            return "horizontal-reorder";
        case AnchorShape::Plus:
            return "plus";
    }
    return "unknown";
}

std::string FormatAnchorSubject(const AppConfig& config, const LayoutEditAnchorKey& key) {
    if (const auto parameter = LayoutEditAnchorParameter(key); parameter.has_value()) {
        return FormatLayoutEditParameterDetail(*parameter);
    }
    if (const auto metric = LayoutEditAnchorMetricKey(key); metric.has_value()) {
        return "metric binding " + metric->metricId;
    }
    if (const auto title = LayoutEditAnchorCardTitleKey(key); title.has_value()) {
        return "card title " + title->cardId;
    }
    if (const auto order = LayoutEditAnchorMetricListOrderKey(key); order.has_value()) {
        return "metric list order " + FormatLayoutConfigPath(config, order->editCardId, order->nodePath);
    }
    if (const auto order = LayoutEditAnchorContainerChildOrderKey(key); order.has_value()) {
        return "container child order " + FormatLayoutConfigPath(config, order->editCardId, order->nodePath);
    }
    return "unknown anchor subject";
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

RenderRect UnionRect(const RenderRect& left, const RenderRect& right) {
    if (left.IsEmpty()) {
        return right;
    }
    if (right.IsEmpty()) {
        return left;
    }
    return RenderRect{(std::min)(left.left, right.left),
        (std::min)(left.top, right.top),
        (std::max)(left.right, right.right),
        (std::max)(left.bottom, right.bottom)};
}

bool SameRect(const RenderRect& left, const RenderRect& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

bool IsFontEditParameter(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    return descriptor.has_value() && descriptor->valueFormat == configschema::ValueFormat::FontSpec;
}

bool IsColorEditParameter(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    return descriptor.has_value() && descriptor->valueFormat == configschema::ValueFormat::ColorHex;
}

bool UseWholeWidgetSelectionOutline(LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::GaugeOuterPadding:
        case LayoutEditParameter::GaugeRingThickness:
        case LayoutEditParameter::ThroughputGuideStrokeWidth:
        case LayoutEditParameter::ThroughputPlotStrokeWidth:
        case LayoutEditParameter::ThroughputLeaderDiameter:
            return true;
        default:
            return false;
    }
}

bool UseWholeDashboardSelectionOutline(LayoutEditParameter parameter) {
    return parameter == LayoutEditParameter::DashboardOuterMargin ||
           parameter == LayoutEditParameter::DashboardRowGap || parameter == LayoutEditParameter::DashboardColumnGap;
}

bool UseAllCardsSelectionOutline(LayoutEditParameter parameter) {
    return parameter == LayoutEditParameter::CardRowGap || parameter == LayoutEditParameter::CardColumnGap;
}

bool SuppressAnchorTargetSelectionOutline(LayoutEditParameter parameter) {
    return parameter == LayoutEditParameter::GaugeOuterPadding || parameter == LayoutEditParameter::GaugeRingThickness;
}

RenderRect TextAnchorRectForShape(const DashboardRenderer& renderer, const RenderRect& textRect, AnchorShape shape) {
    const int anchorSize = std::max(4, renderer.ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    if (shape == AnchorShape::Wedge) {
        const int outsideLeft = std::max(1, renderer.ScaleLogical(2));
        const int outsideTop = std::max(1, renderer.ScaleLogical(1));
        const int insideWidth = std::max(4, renderer.ScaleLogical(5));
        const int insideHeight = std::max(5, renderer.ScaleLogical(6));
        return RenderRect{textRect.left - outsideLeft,
            textRect.top - outsideTop,
            textRect.left + insideWidth,
            textRect.top - outsideTop + insideHeight};
    }

    const int anchorCenterX = textRect.right;
    const int anchorCenterY = textRect.top;
    return RenderRect{anchorCenterX - anchorHalf,
        anchorCenterY - anchorHalf,
        anchorCenterX - anchorHalf + anchorSize,
        anchorCenterY - anchorHalf + anchorSize};
}

UINT GetIconResourceId(std::string_view iconName) {
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

Microsoft::WRL::ComPtr<IWICBitmapSource> TintMonochromeBitmapSource(
    IWICImagingFactory* wicFactory, IWICBitmapSource* source, RenderColor color) {
    Microsoft::WRL::ComPtr<IWICBitmapSource> tintedBitmap;
    if (wicFactory == nullptr || source == nullptr) {
        return tintedBitmap;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(source->GetSize(&width, &height)) || width == 0 || height == 0) {
        return tintedBitmap;
    }

    const UINT stride = width * 4;
    std::vector<BYTE> pixels(static_cast<size_t>(stride) * static_cast<size_t>(height));
    if (FAILED(source->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) {
        return tintedBitmap;
    }

    for (size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        const BYTE alpha = static_cast<BYTE>((static_cast<unsigned int>(pixels[offset + 3]) * color.a) / 255u);
        pixels[offset + 0] = static_cast<BYTE>((static_cast<unsigned int>(color.b) * alpha) / 255u);
        pixels[offset + 1] = static_cast<BYTE>((static_cast<unsigned int>(color.g) * alpha) / 255u);
        pixels[offset + 2] = static_cast<BYTE>((static_cast<unsigned int>(color.r) * alpha) / 255u);
        pixels[offset + 3] = alpha;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    if (FAILED(wicFactory->CreateBitmapFromMemory(width,
            height,
            GUID_WICPixelFormat32bppPBGRA,
            stride,
            static_cast<UINT>(pixels.size()),
            pixels.data(),
            bitmap.GetAddressOf())) ||
        bitmap == nullptr) {
        return tintedBitmap;
    }

    tintedBitmap = bitmap;
    return tintedBitmap;
}

}  // namespace

DashboardRenderer::DashboardRenderer(Trace& trace)
    : trace_(trace), palette_(std::make_unique<DashboardPalette>(config_.layout.colors)),
      layoutResolver_(std::make_unique<DashboardLayoutResolver>()), d2dCache_(std::make_unique<DashboardD2DCache>()),
      textWidthCache_(std::make_unique<DashboardTextWidthCache>()) {}

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

void DashboardRenderer::SetConfig(const AppConfig& config) {
    lastError_.clear();
    const bool paletteChanged = config_.layout.colors != config.layout.colors;
    const bool iconInputsChanged = config_.layout.colors.iconColor != config.layout.colors.iconColor ||
                                   !SameIconInputs(config_.layout.cards, config.layout.cards);
    const bool textFormatsChanged = config_.layout.fonts != config.layout.fonts;
    const bool metricsChanged = config_.layout.metrics != config.layout.metrics;
    if (metricsChanged) {
        InvalidateMetricSourceCache();
        metricDefinitionCache_.clear();
        metricSampleValueTextCache_.clear();
    }
    config_ = config;
    if (paletteChanged) {
        RebuildPalette();
    }
    if (dwriteFactory_ != nullptr) {
        const bool iconsReady = !iconInputsChanged || LoadIcons();
        const bool textReady = !textFormatsChanged || RebuildTextFormatsAndMetrics();
        if (!iconsReady || !textReady || !ResolveLayout()) {
            lastError_ = lastError_.empty() ? "renderer:reconfigure_failed" : lastError_;
        } else {
            d2dFirstDrawWarmupPending_ = false;
        }
    }
}

void DashboardRenderer::SetRenderScale(double scale) {
    lastError_.clear();
    const double nextScale = std::clamp(scale, 0.1, 16.0);
    if (std::abs(renderScale_ - nextScale) < 0.0001) {
        return;
    }
    renderScale_ = nextScale;
    if (dwriteFactory_ != nullptr) {
        if (!RebuildTextFormatsAndMetrics() || !ResolveLayout()) {
            lastError_ = lastError_.empty() ? "renderer:rescale_failed" : lastError_;
        } else {
            d2dFirstDrawWarmupPending_ = false;
        }
    }
}

void DashboardRenderer::SetImmediatePresent(bool enabled) {
    if (d2dImmediatePresent_ == enabled) {
        return;
    }
    d2dImmediatePresent_ = enabled;
    DiscardWindowRenderTarget("present_mode_change");
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

std::vector<LayoutEditWidgetGuide>& DashboardRenderer::WidgetEditGuidesMutable() {
    return layoutResolver_->widgetEditGuides_;
}

std::vector<LayoutEditGapAnchor>& DashboardRenderer::GapEditAnchorsMutable() {
    return layoutResolver_->gapEditAnchors_;
}

std::optional<MetricListReorderOverlayState> DashboardRenderer::ActiveMetricListReorderDrag(
    const LayoutEditWidgetIdentity& widget) const {
    if (activeOverlayState_ == nullptr || !activeOverlayState_->activeMetricListReorderDrag.has_value()) {
        return std::nullopt;
    }

    const MetricListReorderOverlayState& drag = *activeOverlayState_->activeMetricListReorderDrag;
    if (drag.widget.kind != widget.kind || drag.widget.renderCardId != widget.renderCardId ||
        drag.widget.editCardId != widget.editCardId || drag.widget.nodePath != widget.nodePath) {
        return std::nullopt;
    }
    return drag;
}

int DashboardRenderer::WindowWidth() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.width));
}

int DashboardRenderer::WindowHeight() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.height));
}

const std::vector<LayoutEditGuide>& DashboardRenderer::LayoutEditGuides() const {
    return layoutResolver_->layoutEditGuides_;
}

const std::vector<LayoutEditWidgetGuide>& DashboardRenderer::WidgetEditGuides() const {
    return layoutResolver_->widgetEditGuides_;
}

const std::vector<LayoutEditGapAnchor>& DashboardRenderer::GapEditAnchors() const {
    return layoutResolver_->gapEditAnchors_;
}

int DashboardRenderer::LayoutSimilarityThreshold() const {
    return std::max(0, ScaleLogical(config_.layout.layoutEditor.sizeSimilarityThreshold));
}

void DashboardRenderer::ResolveNodeWidgets(
    const LayoutNodeConfig& node, const RenderRect& rect, std::vector<WidgetLayout>& widgets, bool instantiateWidgets) {
    layoutResolver_->ResolveNodeWidgets(*this, node, rect, widgets, instantiateWidgets);
}

void DashboardRenderer::BuildWidgetEditGuides() {
    layoutResolver_->BuildWidgetEditGuides(*this);
}

void DashboardRenderer::BuildStaticEditableAnchors() {
    layoutResolver_->BuildStaticEditableAnchors(*this);
}

void DashboardRenderer::AddLayoutEditGuide(const LayoutNodeConfig& node,
    const RenderRect& rect,
    const std::vector<RenderRect>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    layoutResolver_->AddLayoutEditGuide(*this, node, rect, childRects, gap, renderCardId, editCardId, nodePath);
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<WidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    bool instantiateWidgets) {
    layoutResolver_->ResolveNodeWidgetsInternal(
        *this, node, rect, widgets, cardReferenceStack, renderCardId, editCardId, nodePath, instantiateWidgets);
}

bool DashboardRenderer::ResolveLayout(bool includeWidgetState) {
    return layoutResolver_->ResolveLayout(*this, includeWidgetState);
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
    RenderColorId color,
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
    d2dActiveRenderTarget_->DrawText(wideText.c_str(),
        static_cast<UINT32>(wideText.size()),
        textFormat,
        D2DRectFromRenderRect(rect),
        brush,
        drawOptions);
    return result;
}

void DashboardRenderer::DrawText(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    RenderColorId color,
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
    d2dActiveRenderTarget_->DrawText(wideText.c_str(),
        static_cast<UINT32>(wideText.size()),
        textFormat,
        D2DRectFromRenderRect(rect),
        brush,
        drawOptions);
}

void DashboardRenderer::DrawHoveredWidgetHighlight(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }
    const std::optional<RenderRect> hoveredRect = FindHoveredWidgetOutlineRect(overlayState);
    if (!hoveredRect.has_value() || hoveredRect->IsEmpty()) {
        return;
    }
    const_cast<DashboardRenderer*>(this)->DrawSolidRect(*hoveredRect, RenderStroke::Solid(RenderColorId::LayoutGuide));
}

void DashboardRenderer::DrawHoveredEditableAnchorHighlight(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }

    std::vector<std::pair<LayoutEditAnchorRegion, bool>> highlights;
    const auto appendHighlight = [&](const LayoutEditAnchorRegion& region, bool active) {
        const auto existing = std::find_if(highlights.begin(), highlights.end(), [&](const auto& entry) {
            return MatchesEditableAnchorKey(entry.first.key, region.key);
        });
        if (existing == highlights.end()) {
            highlights.push_back({region, active});
            return;
        }
        existing->second = existing->second || active;
    };
    const auto appendRelatedHighlights = [&](const LayoutEditAnchorRegion& source, bool active) {
        const auto collect = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!::MatchesWidgetIdentity(region.key.widget, source.key.widget) ||
                    !SameRect(region.targetRect, source.targetRect)) {
                    continue;
                }
                LayoutEditAnchorRegion highlightedRegion = region;
                if (!MatchesEditableAnchorKey(region.key, source.key)) {
                    highlightedRegion.drawTargetOutline = false;
                }
                appendHighlight(highlightedRegion, active && MatchesEditableAnchorKey(region.key, source.key));
            }
        };
        collect(layoutResolver_->staticEditableAnchorRegions_);
        collect(layoutResolver_->dynamicEditableAnchorRegions_);
    };
    const auto appendByKey = [&](const std::optional<LayoutEditAnchorKey>& key, bool active) {
        if (!key.has_value()) {
            return;
        }
        const auto region = FindEditableAnchorRegion(*key);
        if (region.has_value()) {
            appendRelatedHighlights(*region, active);
        }
    };
    if (overlayState.selectedTreeHighlight.has_value()) {
        const auto collectSelected = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) ||
                    (std::holds_alternative<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) &&
                        std::get<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) ==
                            LayoutEditSelectionHighlightSpecial::AllTexts &&
                        LayoutEditAnchorParameter(region.key).has_value() &&
                        IsFontEditParameter(*LayoutEditAnchorParameter(region.key)))) {
                    appendHighlight(region, true);
                }
            }
        };
        collectSelected(layoutResolver_->staticEditableAnchorRegions_);
        collectSelected(layoutResolver_->dynamicEditableAnchorRegions_);
    }
    if (overlayState.hoveredEditableWidget.has_value()) {
        const auto collectHovered = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered) {
                    continue;
                }
                if (region.key.widget.kind != LayoutEditWidgetIdentity::Kind::Widget ||
                    region.key.widget.renderCardId != overlayState.hoveredEditableWidget->renderCardId ||
                    region.key.widget.editCardId != overlayState.hoveredEditableWidget->editCardId ||
                    region.key.widget.nodePath != overlayState.hoveredEditableWidget->nodePath) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHovered(layoutResolver_->staticEditableAnchorRegions_);
        collectHovered(layoutResolver_->dynamicEditableAnchorRegions_);
    }
    if (overlayState.hoveredEditableCard.has_value()) {
        const auto collectHoveredCard = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered ||
                    region.key.widget.kind != LayoutEditWidgetIdentity::Kind::CardChrome ||
                    region.key.widget.renderCardId != overlayState.hoveredEditableCard->renderCardId ||
                    region.key.widget.editCardId != overlayState.hoveredEditableCard->editCardId) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHoveredCard(layoutResolver_->staticEditableAnchorRegions_);
        collectHoveredCard(layoutResolver_->dynamicEditableAnchorRegions_);
    }
    appendByKey(overlayState.hoveredEditableAnchor, false);
    appendByKey(overlayState.activeEditableAnchor, true);
    if (highlights.empty()) {
        return;
    }
    const std::optional<RenderRect> hoveredWidgetOutlineRect = FindHoveredWidgetOutlineRect(overlayState);
    for (const auto& [highlighted, active] : highlights) {
        const RenderColorId outlineColor = active ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        bool drawTargetOutline = highlighted.drawTargetOutline && !highlighted.targetRect.IsEmpty();
        if (!active && drawTargetOutline && hoveredWidgetOutlineRect.has_value() &&
            SameRect(highlighted.targetRect, *hoveredWidgetOutlineRect)) {
            drawTargetOutline = false;
        }
        if (drawTargetOutline) {
            const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(highlighted.targetRect, outlineColor, active);
        }

        if (highlighted.shape == AnchorShape::Circle) {
            const float outlineWidth =
                static_cast<float>(active ? (std::max)(2, ScaleLogical(2)) : (std::max)(1, ScaleLogical(1)));
            const_cast<DashboardRenderer*>(this)->DrawSolidEllipse(
                highlighted.anchorRect, RenderStroke::Solid(outlineColor, outlineWidth));
        } else if (highlighted.shape == AnchorShape::Diamond) {
            const_cast<DashboardRenderer*>(this)->FillSolidDiamond(highlighted.anchorRect, outlineColor);
        } else if (highlighted.shape == AnchorShape::Wedge) {
            const float outlineWidth =
                static_cast<float>(active ? (std::max)(2, ScaleLogical(2)) : (std::max)(1, ScaleLogical(1)));
            const RenderPoint topRight{highlighted.anchorRect.right, highlighted.anchorRect.top};
            const RenderPoint bottomLeft{highlighted.anchorRect.left, highlighted.anchorRect.bottom};
            const RenderPoint bottomRight{highlighted.anchorRect.right, highlighted.anchorRect.bottom};
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                bottomLeft, bottomRight, RenderStroke::Solid(outlineColor, outlineWidth));
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                topRight, bottomRight, RenderStroke::Solid(outlineColor, outlineWidth));
        } else if (highlighted.shape == AnchorShape::VerticalReorder ||
                   highlighted.shape == AnchorShape::HorizontalReorder) {
            const float outlineWidth =
                static_cast<float>(active ? (std::max)(2, ScaleLogical(2)) : (std::max)(1, ScaleLogical(1)));
            const int centerX = highlighted.anchorRect.left +
                                (std::max<LONG>(0, highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
            const int centerY = highlighted.anchorRect.top +
                                (std::max<LONG>(0, highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
            const int gapHalf = (std::max)(1, ScaleLogical(1));
            const auto stroke = RenderStroke::Solid(outlineColor, outlineWidth);
            if (highlighted.shape == AnchorShape::HorizontalReorder) {
                const int halfHeight =
                    (std::max)(1, static_cast<int>(highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
                const RenderPoint leftApex{highlighted.anchorRect.left, centerY};
                const RenderPoint leftTop{centerX - gapHalf, centerY - halfHeight};
                const RenderPoint leftBottom{centerX - gapHalf, centerY + halfHeight};
                const RenderPoint rightApex{highlighted.anchorRect.right, centerY};
                const RenderPoint rightTop{centerX + gapHalf, centerY - halfHeight};
                const RenderPoint rightBottom{centerX + gapHalf, centerY + halfHeight};
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(leftApex, leftTop, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(leftTop, leftBottom, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(leftBottom, leftApex, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(rightTop, rightApex, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(rightApex, rightBottom, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(rightBottom, rightTop, stroke);
            } else {
                const int halfWidth =
                    (std::max)(1, static_cast<int>(highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
                const RenderPoint upApex{centerX, highlighted.anchorRect.top};
                const RenderPoint upLeft{centerX - halfWidth, centerY - gapHalf};
                const RenderPoint upRight{centerX + halfWidth, centerY - gapHalf};
                const RenderPoint downApex{centerX, highlighted.anchorRect.bottom};
                const RenderPoint downLeft{centerX - halfWidth, centerY + gapHalf};
                const RenderPoint downRight{centerX + halfWidth, centerY + gapHalf};
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(upApex, upLeft, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(upLeft, upRight, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(upRight, upApex, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(downLeft, downApex, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(downApex, downRight, stroke);
                const_cast<DashboardRenderer*>(this)->DrawSolidLine(downRight, downLeft, stroke);
            }
        } else if (highlighted.shape == AnchorShape::Plus) {
            const float outlineWidth =
                static_cast<float>(active ? (std::max)(2, ScaleLogical(2)) : (std::max)(1, ScaleLogical(1)));
            const int centerX = highlighted.anchorRect.left +
                                (std::max<LONG>(0, highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
            const int centerY = highlighted.anchorRect.top +
                                (std::max<LONG>(0, highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
            const int halfWidth =
                (std::max)(2, static_cast<int>(highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
            const int halfHeight =
                (std::max)(2, static_cast<int>(highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
            const auto stroke = RenderStroke::Solid(outlineColor, outlineWidth);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{centerX - halfWidth, centerY}, RenderPoint{centerX + halfWidth, centerY}, stroke);
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{centerX, centerY - halfHeight}, RenderPoint{centerX, centerY + halfHeight}, stroke);
        } else {
            const_cast<DashboardRenderer*>(this)->FillSolidRect(highlighted.anchorRect, outlineColor);
        }
    }
}

std::optional<RenderRect> DashboardRenderer::FindHoveredWidgetOutlineRect(
    const DashboardOverlayState& overlayState) const {
    if (overlayState.hoveredEditableWidget.has_value() &&
        overlayState.hoveredEditableWidget->kind == LayoutEditWidgetIdentity::Kind::Widget) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                if (MatchesWidgetIdentity(widget, *overlayState.hoveredEditableWidget)) {
                    return widget.rect;
                }
            }
        }
    }

    if (overlayState.hoveredEditableCard.has_value() &&
        overlayState.hoveredEditableCard->kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            const LayoutEditWidgetIdentity cardIdentity{
                card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
            if (MatchesCardChromeSelectionIdentity(*overlayState.hoveredEditableCard, cardIdentity) &&
                !card.chromeLayout.titleRect.IsEmpty()) {
                return card.chromeLayout.iconRect.IsEmpty()
                           ? card.chromeLayout.titleRect
                           : UnionRect(card.chromeLayout.iconRect, card.chromeLayout.titleRect);
            }
        }
    }

    return std::nullopt;
}

void DashboardRenderer::DrawSelectedColorEditHighlights(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawSelectedTreeHighlight()) {
        return;
    }

    std::vector<RenderRect> highlightedRects;
    const auto appendRect = [&](const RenderRect& rect) {
        if (rect.IsEmpty()) {
            return;
        }
        const auto existing =
            std::find_if(highlightedRects.begin(), highlightedRects.end(), [&](const RenderRect& candidate) {
                return candidate.left == rect.left && candidate.top == rect.top && candidate.right == rect.right &&
                       candidate.bottom == rect.bottom;
            });
        if (existing == highlightedRects.end()) {
            highlightedRects.push_back(rect);
        }
    };
    const auto collect = [&](const std::vector<LayoutEditColorRegion>& regions) {
        for (const auto& region : regions) {
            if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region)) {
                appendRect(region.targetRect);
            }
        }
    };
    collect(layoutResolver_->staticColorEditRegions_);
    collect(layoutResolver_->dynamicColorEditRegions_);
    for (const auto& rect : highlightedRects) {
        const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(rect, RenderColorId::ActiveEdit, true);
    }
}

void DashboardRenderer::DrawSelectedTreeNodeHighlight(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawSelectedTreeHighlight()) {
        return;
    }

    const RenderColorId color = RenderColorId::ActiveEdit;
    std::vector<RenderRect> selectedRects;
    bool drawDashboardBoundsOutline = false;
    const auto appendRect = [&](const RenderRect& rect) {
        if (rect.IsEmpty()) {
            return;
        }
        const auto existing =
            std::find_if(selectedRects.begin(), selectedRects.end(), [&](const RenderRect& candidate) {
                return candidate.left == rect.left && candidate.top == rect.top && candidate.right == rect.right &&
                       candidate.bottom == rect.bottom;
            });
        if (existing == selectedRects.end()) {
            selectedRects.push_back(rect);
        }
    };
    const auto appendWidgetRectsForIdentity = [&](const LayoutEditWidgetIdentity& identity) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                const LayoutEditWidgetIdentity candidateIdentity{widget.cardId, widget.editCardId, widget.nodePath};
                if (::MatchesWidgetIdentity(identity, candidateIdentity)) {
                    appendRect(widget.rect);
                }
            }
        }
    };
    if (const auto* focusKey = std::get_if<LayoutEditFocusKey>(&*overlayState.selectedTreeHighlight)) {
        if (const auto* parameter = std::get_if<LayoutEditParameter>(focusKey)) {
            if (UseWholeDashboardSelectionOutline(*parameter)) {
                drawDashboardBoundsOutline = true;
            }
            if (UseAllCardsSelectionOutline(*parameter)) {
                for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
                    appendRect(card.rect);
                }
            }
        }
    }
    for (const auto& guide : layoutResolver_->layoutEditGuides_) {
        const bool matchesFocus = MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool matchesContainer =
            std::holds_alternative<LayoutContainerEditKey>(*overlayState.selectedTreeHighlight) &&
            MatchesLayoutContainerEditKey(std::get<LayoutContainerEditKey>(*overlayState.selectedTreeHighlight),
                {guide.editCardId, guide.nodePath});
        if (matchesFocus || matchesContainer) {
            appendRect(guide.containerRect);
        }
    }
    for (const auto& guide : layoutResolver_->widgetEditGuides_) {
        if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide)) {
            appendRect(guide.widgetRect);
        }
    }
    const auto collectAnchorTargets = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
        for (const auto& region : regions) {
            if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) ||
                (std::holds_alternative<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) &&
                    std::get<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) ==
                        LayoutEditSelectionHighlightSpecial::AllTexts &&
                    LayoutEditAnchorParameter(region.key).has_value() &&
                    IsFontEditParameter(*LayoutEditAnchorParameter(region.key)))) {
                if (const auto parameter = LayoutEditAnchorParameter(region.key); parameter.has_value()) {
                    if (!SuppressAnchorTargetSelectionOutline(*parameter)) {
                        appendRect(region.targetRect);
                    }
                    if (UseWholeWidgetSelectionOutline(*parameter)) {
                        appendWidgetRectsForIdentity(region.key.widget);
                    }
                } else {
                    appendRect(region.targetRect);
                }
            }
        }
    };
    collectAnchorTargets(layoutResolver_->staticEditableAnchorRegions_);
    collectAnchorTargets(layoutResolver_->dynamicEditableAnchorRegions_);
    for (const auto& rect : selectedRects) {
        const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(rect, color, true);
    }
    if (drawDashboardBoundsOutline) {
        const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(
            RenderRect{
                0, 0, layoutResolver_->resolvedLayout_.windowWidth, layoutResolver_->resolvedLayout_.windowHeight},
            color,
            true,
            false);
    }

    if (const auto* widgetClass = std::get_if<WidgetClass>(&*overlayState.selectedTreeHighlight)) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                if (widget.widget != nullptr && widget.widget->Class() == *widgetClass) {
                    const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(widget.rect, color, true);
                }
            }
        }
        return;
    }

    if (const auto* widgetIdentity = std::get_if<LayoutEditWidgetIdentity>(&*overlayState.selectedTreeHighlight)) {
        if (widgetIdentity->kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            std::vector<RenderRect> embeddedInstanceRects;
            const auto appendEmbeddedRect = [&](const RenderRect& rect) {
                if (rect.IsEmpty()) {
                    return;
                }
                const auto existing = std::find_if(
                    embeddedInstanceRects.begin(), embeddedInstanceRects.end(), [&](const RenderRect& candidate) {
                        return candidate.left == rect.left && candidate.top == rect.top &&
                               candidate.right == rect.right && candidate.bottom == rect.bottom;
                    });
                if (existing == embeddedInstanceRects.end()) {
                    embeddedInstanceRects.push_back(rect);
                }
            };
            for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
                const LayoutEditWidgetIdentity cardIdentity{
                    card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                if (MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                    const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(card.rect, color, true);
                }
            }
            for (const auto& guide : layoutResolver_->layoutEditGuides_) {
                const LayoutEditWidgetIdentity cardIdentity{
                    guide.renderCardId, guide.editCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                if (guide.renderCardId.empty() || guide.renderCardId == guide.editCardId || !guide.nodePath.empty() ||
                    !MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                    continue;
                }
                appendEmbeddedRect(guide.containerRect);
            }
            if (embeddedInstanceRects.empty()) {
                for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
                    RenderRect embeddedBounds{};
                    for (const auto& widget : card.widgets) {
                        const LayoutEditWidgetIdentity cardIdentity{
                            widget.cardId, widget.editCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                        if (widget.cardId == widget.editCardId ||
                            !MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                            continue;
                        }
                        embeddedBounds = UnionRect(embeddedBounds, widget.rect);
                    }
                    appendEmbeddedRect(embeddedBounds);
                }
            }
            for (const auto& rect : embeddedInstanceRects) {
                const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(rect, color, true);
            }
            return;
        }
    }

    if (const auto* special = std::get_if<LayoutEditSelectionHighlightSpecial>(&*overlayState.selectedTreeHighlight)) {
        if (*special == LayoutEditSelectionHighlightSpecial::AllCards) {
            for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
                const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(card.rect, color, true);
            }
            return;
        }
        if (*special == LayoutEditSelectionHighlightSpecial::AllTexts) {
            for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
                for (const auto& widget : card.widgets) {
                    if (widget.widget != nullptr && widget.widget->Class() == WidgetClass::Text) {
                        const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(widget.rect, color, true);
                    }
                }
            }
        }
        if (*special == LayoutEditSelectionHighlightSpecial::DashboardBounds) {
            const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(
                RenderRect{
                    0, 0, layoutResolver_->resolvedLayout_.windowWidth, layoutResolver_->resolvedLayout_.windowHeight},
                color,
                true,
                false);
            return;
        }
    }
}

void DashboardRenderer::DrawLayoutEditGuides(const DashboardOverlayState& overlayState) const {
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_->layoutEditGuides_.empty()) {
        return;
    }

    std::vector<std::pair<RenderRect, bool>> containerHighlights;
    const auto appendContainerHighlight = [&](const RenderRect& rect, bool active) {
        if (rect.IsEmpty()) {
            return;
        }
        const auto existing =
            std::find_if(containerHighlights.begin(), containerHighlights.end(), [&](const auto& entry) {
                return entry.first.left == rect.left && entry.first.top == rect.top &&
                       entry.first.right == rect.right && entry.first.bottom == rect.bottom;
            });
        if (existing == containerHighlights.end()) {
            containerHighlights.push_back({rect, active});
            return;
        }
        existing->second = existing->second || active;
    };
    if (overlayState.hoveredLayoutEditGuide.has_value()) {
        appendContainerHighlight(overlayState.hoveredLayoutEditGuide->containerRect, false);
    }
    if (overlayState.activeLayoutEditGuide.has_value()) {
        appendContainerHighlight(overlayState.activeLayoutEditGuide->containerRect, true);
    }
    for (const auto& [rect, active] : containerHighlights) {
        const RenderColorId color = active ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        const_cast<DashboardRenderer*>(this)->DrawDottedHighlightRect(rect, color, active);
    }

    const int lineWidth = (std::max)(1, ScaleLogical(1));
    const int activeLineWidth = (std::max)(lineWidth + 1, ScaleLogical(2));
    for (const auto& guide : layoutResolver_->layoutEditGuides_) {
        const bool active = overlayState.activeLayoutEditGuide.has_value() &&
                            MatchesLayoutEditGuide(guide, *overlayState.activeLayoutEditGuide);
        const bool hoveredGuide = overlayState.hoveredLayoutEditGuide.has_value() &&
                                  MatchesLayoutEditGuide(guide, *overlayState.hoveredLayoutEditGuide);
        const bool selected = overlayState.selectedTreeHighlight.has_value() &&
                              MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool emphasized = active || selected;
        if (!emphasized && !hoveredGuide && !overlayState.hoverOnExposedDashboard) {
            continue;
        }
        const RenderColorId color = emphasized ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        const RenderPoint start{guide.lineRect.left, guide.lineRect.top};
        const RenderPoint end = guide.axis == LayoutGuideAxis::Vertical
                                    ? RenderPoint{guide.lineRect.left, guide.lineRect.bottom}
                                    : RenderPoint{guide.lineRect.right, guide.lineRect.top};
        const_cast<DashboardRenderer*>(this)->DrawSolidLine(
            start, end, RenderStroke::Solid(color, static_cast<float>(emphasized ? activeLineWidth : lineWidth)));
    }
}

void DashboardRenderer::DrawWidgetEditGuides(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_->widgetEditGuides_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const LayoutEditWidgetGuide& guide) {
        if (overlayState.activeWidgetEditGuide.has_value()) {
            return guide.widget.kind == overlayState.activeWidgetEditGuide->widget.kind &&
                   guide.widget.renderCardId == overlayState.activeWidgetEditGuide->widget.renderCardId &&
                   guide.widget.editCardId == overlayState.activeWidgetEditGuide->widget.editCardId &&
                   guide.widget.nodePath == overlayState.activeWidgetEditGuide->widget.nodePath;
        }
        if (overlayState.selectedTreeHighlight.has_value() &&
            MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide)) {
            return true;
        }
        if (guide.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            return overlayState.hoveredEditableCard.has_value() &&
                   guide.widget.renderCardId == overlayState.hoveredEditableCard->renderCardId &&
                   guide.widget.editCardId == overlayState.hoveredEditableCard->editCardId;
        }
        if (!overlayState.hoveredEditableWidget.has_value()) {
            return false;
        }
        return guide.widget.kind == LayoutEditWidgetIdentity::Kind::Widget &&
               guide.widget.renderCardId == overlayState.hoveredEditableWidget->renderCardId &&
               guide.widget.editCardId == overlayState.hoveredEditableWidget->editCardId &&
               guide.widget.nodePath == overlayState.hoveredEditableWidget->nodePath;
    };

    const int lineWidth = (std::max)(1, ScaleLogical(1));
    const int activeLineWidth = (std::max)(lineWidth + 1, ScaleLogical(2));
    for (const auto& guide : layoutResolver_->widgetEditGuides_) {
        if (!shouldDraw(guide)) {
            continue;
        }
        const bool active = overlayState.activeWidgetEditGuide.has_value() &&
                            MatchesWidgetEditGuide(guide, *overlayState.activeWidgetEditGuide);
        const bool selected = !overlayState.activeWidgetEditGuide.has_value() &&
                              overlayState.selectedTreeHighlight.has_value() &&
                              MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool emphasized = active || selected;
        const RenderColorId color = emphasized ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        const_cast<DashboardRenderer*>(this)->DrawSolidLine(guide.drawStart,
            guide.drawEnd,
            RenderStroke::Solid(color, static_cast<float>(emphasized ? activeLineWidth : lineWidth)));
    }
}

void DashboardRenderer::DrawGapEditAnchors(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_->gapEditAnchors_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const LayoutEditGapAnchor& anchor) {
        if (overlayState.activeGapEditAnchor.has_value()) {
            if (anchor.key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
                return overlayState.activeGapEditAnchor->widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome;
            }
            return overlayState.activeGapEditAnchor->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome &&
                   anchor.key.widget.renderCardId == overlayState.activeGapEditAnchor->widget.renderCardId &&
                   anchor.key.widget.editCardId == overlayState.activeGapEditAnchor->widget.editCardId;
        }
        const bool selected = overlayState.selectedTreeHighlight.has_value() &&
                              MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, anchor.key);
        if (selected) {
            return true;
        }
        if (overlayState.selectedTreeHighlight.has_value() && !overlayState.hoverOnExposedDashboard &&
            !overlayState.hoveredLayoutCard.has_value() && !overlayState.hoveredGapEditAnchor.has_value()) {
            return false;
        }
        if (overlayState.hoverOnExposedDashboard) {
            return true;
        }
        if (anchor.key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
            return true;
        }
        return overlayState.hoveredLayoutCard.has_value() &&
               anchor.key.widget.renderCardId == overlayState.hoveredLayoutCard->renderCardId &&
               anchor.key.widget.editCardId == overlayState.hoveredLayoutCard->editCardId;
    };

    const int capHalf = (std::max)(2, ScaleLogical(4));
    const int lineWidth = (std::max)(1, ScaleLogical(1));
    const int activeLineWidth = (std::max)(lineWidth + 1, ScaleLogical(2));
    const int handleOutline = (std::max)(1, ScaleLogical(1));
    for (const auto& anchor : layoutResolver_->gapEditAnchors_) {
        if (!shouldDraw(anchor)) {
            continue;
        }
        const bool active = overlayState.activeGapEditAnchor.has_value() &&
                            MatchesGapEditAnchorKey(anchor.key, *overlayState.activeGapEditAnchor);
        const bool selected = !overlayState.activeGapEditAnchor.has_value() &&
                              overlayState.selectedTreeHighlight.has_value() &&
                              MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, anchor.key);
        const bool hovered = overlayState.hoveredGapEditAnchor.has_value() &&
                             MatchesGapEditAnchorKey(anchor.key, *overlayState.hoveredGapEditAnchor);
        const bool emphasized = active || selected;
        const RenderColorId color = emphasized ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        const float strokeWidth = static_cast<float>(emphasized ? activeLineWidth : lineWidth);

        const_cast<DashboardRenderer*>(this)->DrawSolidLine(
            anchor.drawStart, anchor.drawEnd, RenderStroke::Solid(color, strokeWidth));
        if (anchor.axis == LayoutGuideAxis::Vertical) {
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawStart.x - capHalf, anchor.drawStart.y},
                RenderPoint{anchor.drawStart.x + capHalf, anchor.drawStart.y},
                RenderStroke::Solid(color, strokeWidth));
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawEnd.x - capHalf, anchor.drawEnd.y},
                RenderPoint{anchor.drawEnd.x + capHalf, anchor.drawEnd.y},
                RenderStroke::Solid(color, strokeWidth));
        } else {
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawStart.x, anchor.drawStart.y - capHalf},
                RenderPoint{anchor.drawStart.x, anchor.drawStart.y + capHalf},
                RenderStroke::Solid(color, strokeWidth));
            const_cast<DashboardRenderer*>(this)->DrawSolidLine(
                RenderPoint{anchor.drawEnd.x, anchor.drawEnd.y - capHalf},
                RenderPoint{anchor.drawEnd.x, anchor.drawEnd.y + capHalf},
                RenderStroke::Solid(color, strokeWidth));
        }

        if (emphasized || hovered || overlayState.hoverOnExposedDashboard) {
            const_cast<DashboardRenderer*>(this)->FillSolidRect(anchor.handleRect, color);
        } else {
            const_cast<DashboardRenderer*>(this)->DrawSolidRect(
                anchor.handleRect, RenderStroke::Solid(color, static_cast<float>(handleOutline)));
        }
    }
}

void DashboardRenderer::DrawDottedHighlightRect(
    const RenderRect& rect, RenderColorId color, bool active, bool outside) const {
    if (rect.IsEmpty()) {
        return;
    }
    const int padding = std::max(1, ScaleLogical(1));
    const RenderRect outlineRect =
        outside ? rect.Inflate(padding, padding)
                : RenderRect{rect.left + padding, rect.top + padding, rect.right - padding, rect.bottom - padding};
    const float outlineWidth =
        static_cast<float>(active ? (std::max)(2, ScaleLogical(2)) : (std::max)(1, ScaleLogical(1)));
    const RenderRect drawRect = outlineRect.IsEmpty() ? rect : outlineRect;
    auto* renderer = const_cast<DashboardRenderer*>(this);
    const int strokeWidth = (std::max)(1, static_cast<int>(std::lround(outlineWidth)));
    const int dotLength = (std::max)(strokeWidth + 1, ScaleLogical(active ? 6 : 5));
    const int gapLength = (std::max)(strokeWidth + 1, ScaleLogical(active ? 5 : 4));

    const auto drawHorizontal = [&](int y, int left, int right) {
        for (int x = left; x < right; x += dotLength + gapLength) {
            const int segmentRight = (std::min)(x + dotLength, right);
            renderer->FillSolidRect(RenderRect{x, y, segmentRight, y + strokeWidth}, color);
        }
    };
    const auto drawVertical = [&](int x, int top, int bottom) {
        for (int y = top; y < bottom; y += dotLength + gapLength) {
            const int segmentBottom = (std::min)(y + dotLength, bottom);
            renderer->FillSolidRect(RenderRect{x, y, x + strokeWidth, segmentBottom}, color);
        }
    };

    drawHorizontal(drawRect.top, drawRect.left, drawRect.right);
    drawHorizontal((std::max)(drawRect.top, drawRect.bottom - strokeWidth), drawRect.left, drawRect.right);
    drawVertical(drawRect.left, drawRect.top, drawRect.bottom);
    drawVertical((std::max)(drawRect.left, drawRect.right - strokeWidth), drawRect.top, drawRect.bottom);
}

int DashboardRenderer::WidgetExtentForAxis(const WidgetLayout& widget, LayoutGuideAxis axis) const {
    return axis == LayoutGuideAxis::Vertical ? std::max(0, static_cast<int>(widget.rect.right - widget.rect.left))
                                             : std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top));
}

bool DashboardRenderer::IsWidgetAffectedByGuide(const WidgetLayout& widget, const LayoutEditGuide& guide) const {
    if (!guide.renderCardId.empty() && widget.cardId != guide.renderCardId) {
        return false;
    }
    return widget.rect.left >= guide.containerRect.left && widget.rect.top >= guide.containerRect.top &&
           widget.rect.right <= guide.containerRect.right && widget.rect.bottom <= guide.containerRect.bottom;
}

bool DashboardRenderer::MatchesWidgetIdentity(
    const WidgetLayout& widget, const LayoutEditWidgetIdentity& identity) const {
    return identity.kind == LayoutEditWidgetIdentity::Kind::Widget && widget.cardId == identity.renderCardId &&
           widget.editCardId == identity.editCardId && widget.nodePath == identity.nodePath;
}

LayoutEditAnchorBinding DashboardRenderer::MakeEditableTextBinding(
    const WidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const {
    LayoutEditAnchorBinding binding;
    binding.key.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    binding.key.subject = parameter;
    binding.key.anchorId = anchorId;
    binding.value = value;
    binding.shape = AnchorShape::Circle;
    binding.dragAxis = AnchorDragAxis::Vertical;
    binding.dragMode = AnchorDragMode::AxisDelta;
    binding.draggable = true;
    return binding;
}

LayoutEditAnchorBinding DashboardRenderer::MakeMetricTextBinding(
    const WidgetLayout& widget, std::string_view metricId, int anchorId) const {
    LayoutEditAnchorBinding binding;
    binding.key.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    binding.key.subject = LayoutMetricEditKey{std::string(metricId)};
    binding.key.anchorId = anchorId;
    binding.value = 0;
    binding.shape = AnchorShape::Wedge;
    binding.dragAxis = AnchorDragAxis::Vertical;
    binding.dragMode = AnchorDragMode::AxisDelta;
    binding.draggable = false;
    return binding;
}

void DashboardRenderer::RegisterEditableAnchorRegion(std::vector<LayoutEditAnchorRegion>& regions,
    const LayoutEditAnchorKey& key,
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
    double dragScale,
    bool draggable,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    if (anchorRect.right <= anchorRect.left || anchorRect.bottom <= anchorRect.top) {
        return;
    }
    LayoutEditAnchorRegion region;
    region.key = key;
    region.targetRect = targetRect;
    region.anchorRect = anchorRect;
    region.shape = shape;
    const int anchorHitInset =
        shape == AnchorShape::Wedge ? std::max(1, ScaleLogical(2)) : std::max(3, ScaleLogical(4));
    region.anchorHitPadding = anchorHitInset;
    region.anchorHitRect = RenderRect{region.anchorRect.left - anchorHitInset,
        region.anchorRect.top - anchorHitInset,
        region.anchorRect.right + anchorHitInset,
        region.anchorRect.bottom + anchorHitInset};
    region.dragAxis = dragAxis;
    region.dragMode = dragMode;
    region.dragOrigin = dragOrigin;
    region.dragScale = dragScale;
    region.draggable = draggable;
    region.showWhenWidgetHovered = showWhenWidgetHovered;
    region.drawTargetOutline = drawTargetOutline;
    region.value = value;
    regions.push_back(std::move(region));
}

void DashboardRenderer::RegisterStaticEditableAnchorRegion(const LayoutEditAnchorKey& key,
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
    double dragScale,
    bool draggable,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    RegisterEditableAnchorRegion(layoutResolver_->staticEditableAnchorRegions_,
        key,
        targetRect,
        anchorRect,
        shape,
        dragAxis,
        dragMode,
        dragOrigin,
        dragScale,
        draggable,
        showWhenWidgetHovered,
        drawTargetOutline,
        value);
}

void DashboardRenderer::RegisterDynamicEditableAnchorRegion(const LayoutEditAnchorKey& key,
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
    double dragScale,
    bool draggable,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterEditableAnchorRegion(layoutResolver_->dynamicEditableAnchorRegions_,
        key,
        targetRect,
        anchorRect,
        shape,
        dragAxis,
        dragMode,
        dragOrigin,
        dragScale,
        draggable,
        showWhenWidgetHovered,
        drawTargetOutline,
        value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
    const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    bool drawTargetOutline) {
    if (text.empty()) {
        return;
    }

    const TextLayoutResult result = MeasureTextBlock(rect, text, style, options);
    const RenderRect anchorRect = TextAnchorRectForShape(*this, result.textRect, editable.shape);
    const RenderPoint anchorOrigin = anchorRect.Center();
    RegisterEditableAnchorRegion(regions,
        editable.key,
        result.textRect,
        anchorRect,
        editable.shape,
        editable.dragAxis,
        editable.dragMode,
        anchorOrigin,
        1.0,
        editable.draggable,
        false,
        drawTargetOutline,
        editable.value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
    const TextLayoutResult& layoutResult,
    const LayoutEditAnchorBinding& editable,
    bool drawTargetOutline) {
    const RenderRect& textRect = layoutResult.textRect;
    if (textRect.right <= textRect.left || textRect.bottom <= textRect.top) {
        return;
    }

    const RenderRect anchorRect = TextAnchorRectForShape(*this, textRect, editable.shape);
    const RenderPoint anchorOrigin = anchorRect.Center();
    RegisterEditableAnchorRegion(regions,
        editable.key,
        textRect,
        anchorRect,
        editable.shape,
        editable.dragAxis,
        editable.dragMode,
        anchorOrigin,
        1.0,
        editable.draggable,
        false,
        drawTargetOutline,
        editable.value);
}

void DashboardRenderer::RegisterStaticTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    bool drawTargetOutline) {
    RegisterTextAnchor(
        layoutResolver_->staticEditableAnchorRegions_, rect, text, style, options, editable, drawTargetOutline);
    if (colorParameter.has_value()) {
        RegisterStaticColorEditRegion(*colorParameter, MeasureTextBlock(rect, text, style, options).textRect);
    }
}

void DashboardRenderer::RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    bool drawTargetOutline) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(layoutResolver_->dynamicEditableAnchorRegions_, layoutResult, editable, drawTargetOutline);
    if (colorParameter.has_value()) {
        RegisterDynamicColorEditRegion(*colorParameter, layoutResult.textRect);
    }
}

void DashboardRenderer::RegisterDynamicTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    bool drawTargetOutline) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(
        layoutResolver_->dynamicEditableAnchorRegions_, rect, text, style, options, editable, drawTargetOutline);
    if (colorParameter.has_value()) {
        RegisterDynamicColorEditRegion(*colorParameter, MeasureTextBlock(rect, text, style, options).textRect);
    }
}

void DashboardRenderer::RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) {
    if (!IsColorEditParameter(parameter) || targetRect.IsEmpty()) {
        return;
    }
    layoutResolver_->staticColorEditRegions_.push_back(LayoutEditColorRegion{parameter, targetRect});
}

void DashboardRenderer::RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_ || !IsColorEditParameter(parameter) ||
        targetRect.IsEmpty()) {
        return;
    }
    layoutResolver_->dynamicColorEditRegions_.push_back(LayoutEditColorRegion{parameter, targetRect});
}

void DashboardRenderer::DrawLayoutSimilarityIndicators(const DashboardOverlayState& overlayState) const {
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }
    const int threshold = LayoutSimilarityThreshold();
    if (threshold <= 0) {
        return;
    }

    struct SimilarityTypeKey {
        WidgetClass widgetClass = WidgetClass::Unknown;
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
    std::vector<const WidgetLayout*> affectedWidgets;
    std::vector<const WidgetLayout*> allWidgets;
    if (overlayState.similarityIndicatorMode == LayoutSimilarityIndicatorMode::AllHorizontal) {
        axis = LayoutGuideAxis::Vertical;
        axisLabel = "horizontal";
        allWidgets = CollectSimilarityIndicatorWidgets(axis);
        affectedWidgets = allWidgets;
    } else if (overlayState.similarityIndicatorMode == LayoutSimilarityIndicatorMode::AllVertical) {
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
        for (const WidgetLayout* widget : allWidgets) {
            if (IsWidgetAffectedByGuide(*widget, guide)) {
                affectedWidgets.push_back(widget);
            }
        }
    }
    if (affectedWidgets.empty()) {
        return;
    }

    std::unordered_map<WidgetClass, std::vector<const WidgetLayout*>> widgetsByClass;
    widgetsByClass.reserve(allWidgets.size());
    for (const WidgetLayout* widget : allWidgets) {
        if (widget->widget == nullptr) {
            continue;
        }
        widgetsByClass[widget->widget->Class()].push_back(widget);
    }

    std::unordered_set<const WidgetLayout*> visibleWidgets;
    visibleWidgets.reserve(allWidgets.size());
    std::unordered_map<const WidgetLayout*, SimilarityTypeKey> exactTypeByWidget;
    exactTypeByWidget.reserve(allWidgets.size());
    for (const WidgetLayout* affected : affectedWidgets) {
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
        for (const WidgetLayout* candidate : classIt->second) {
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
    for (const WidgetLayout* widget : allWidgets) {
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
    for (const WidgetLayout* widget : allWidgets) {
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

    for (const auto& entry : exactTypeOrdinals) {
        WriteTrace("renderer:layout_similarity_group axis=\"" + std::string(axisLabel) +
                   "\" class=" + std::to_string(static_cast<int>(entry.first.widgetClass)) +
                   " extent=" + std::to_string(entry.first.extent) + " ordinal=" + std::to_string(entry.second));
    }

    const RenderColorId color = RenderColorId::LayoutGuide;
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

void DashboardRenderer::DrawMoveOverlay(const DashboardMoveOverlayState& overlayState) {
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

    ID2D1SolidColorBrush* fillBrush = D2DSolidBrush(RenderColorId::Background);
    ID2D1SolidColorBrush* borderBrush = D2DSolidBrush(RenderColorId::Accent);
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
        D2DRectFromRenderRect(overlayRect), static_cast<float>(cornerRadius), static_cast<float>(cornerRadius));
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
        RenderColorId::Accent,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
    y += titleHeight + lineGap;

    const auto drawBodyLine = [&](const std::string& text, bool ellipsis = false) {
        DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, y + bodyHeight},
            text,
            TextStyleId::Small,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center, true, ellipsis));
        y += bodyHeight + lineGap;
    };
    drawBodyLine(monitorText, true);
    drawBodyLine(positionText);
    drawBodyLine(scaleText);
    DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, overlayRect.bottom - padding},
        hintText,
        TextStyleId::Small,
        RenderColorId::MutedText,
        TextLayoutOptions::Wrapped());
}

void DashboardRenderer::DrawResolvedWidget(const WidgetLayout& widget, const MetricSource& metrics) {
    if (widget.widget == nullptr) {
        return;
    }
    widget.widget->Draw(*this, widget, metrics);
}

bool DashboardRenderer::ShouldSkipForContainerChildReorder(const RenderRect& rect) const {
    if (activeOverlayState_ == nullptr || !activeOverlayState_->activeContainerChildReorderDrag.has_value()) {
        return false;
    }
    const ContainerChildReorderOverlayState& drag = *activeOverlayState_->activeContainerChildReorderDrag;
    if (drag.currentIndex < 0 || drag.currentIndex >= static_cast<int>(drag.childRects.size())) {
        return false;
    }
    const RenderRect& childRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
    return rect.left >= childRect.left && rect.top >= childRect.top && rect.right <= childRect.right &&
           rect.bottom <= childRect.bottom;
}

void DashboardRenderer::DrawContainerChildReorderOverlay(const MetricSource& metrics) {
    if (activeOverlayState_ == nullptr || !activeOverlayState_->activeContainerChildReorderDrag.has_value() ||
        d2dActiveRenderTarget_ == nullptr) {
        return;
    }

    const ContainerChildReorderOverlayState& drag = *activeOverlayState_->activeContainerChildReorderDrag;
    if (drag.currentIndex < 0 || drag.currentIndex >= static_cast<int>(drag.childRects.size())) {
        return;
    }

    const RenderRect& childRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
    const int childStart = drag.horizontal ? childRect.left : childRect.top;
    const int offset = drag.mouseCoordinate - drag.dragOffset - childStart;

    D2D1_MATRIX_3X2_F previousTransform{};
    d2dActiveRenderTarget_->GetTransform(&previousTransform);
    const D2D1_MATRIX_3X2_F translation = drag.horizontal
                                              ? D2D1::Matrix3x2F::Translation(static_cast<float>(offset), 0.0f)
                                              : D2D1::Matrix3x2F::Translation(0.0f, static_cast<float>(offset));
    d2dActiveRenderTarget_->SetTransform(translation * previousTransform);
    const bool previousDynamicRegistration = layoutResolver_->dynamicAnchorRegistrationEnabled_;
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (ShouldSkipForContainerChildReorder(card.chrome.rect)) {
            DrawResolvedWidget(card.chrome, metrics);
        }
        for (const auto& widget : card.widgets) {
            if (ShouldSkipForContainerChildReorder(widget.rect)) {
                DrawResolvedWidget(widget, metrics);
            }
        }
    }
    DrawSolidRect(
        childRect, RenderStroke::Dotted(RenderColorId::ActiveEdit, static_cast<float>((std::max)(2, ScaleLogical(2)))));
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = previousDynamicRegistration;
    d2dActiveRenderTarget_->SetTransform(previousTransform);
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot) {
    return DrawWindow(snapshot, DashboardOverlayState{});
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    if (!BeginWindowDraw()) {
        return false;
    }
    DrawDirect2DFrame(snapshot, overlayState);
    EndWindowDraw();
    return lastError_.empty();
}

void DashboardRenderer::DrawDirect2DFrame(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    if (d2dActiveRenderTarget_ == nullptr) {
        lastError_ = "renderer:d2d_target_missing";
        return;
    }

    activeOverlayState_ = &overlayState;
    layoutResolver_->ClearDynamicEditArtifacts();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = overlayState.ShouldRegisterDynamicEditArtifacts();
    const MetricSource& metrics = ResolveMetrics(snapshot);
    d2dActiveRenderTarget_->Clear(palette_->Get(RenderColorId::Background).ToD2DColorF());
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (!ShouldSkipForContainerChildReorder(card.chrome.rect)) {
            DrawResolvedWidget(card.chrome, metrics);
        }
        for (const auto& widget : card.widgets) {
            if (!ShouldSkipForContainerChildReorder(widget.rect)) {
                DrawResolvedWidget(widget, metrics);
            }
        }
    }
    DrawContainerChildReorderOverlay(metrics);
    DrawSelectedColorEditHighlights(overlayState);
    DrawSelectedTreeNodeHighlight(overlayState);
    DrawHoveredWidgetHighlight(overlayState);
    DrawHoveredEditableAnchorHighlight(overlayState);
    DrawLayoutEditGuides(overlayState);
    DrawGapEditAnchors(overlayState);
    DrawWidgetEditGuides(overlayState);
    DrawLayoutSimilarityIndicators(overlayState);
    DrawMoveOverlay(overlayState.moveOverlay);
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    activeOverlayState_ = nullptr;
}

bool DashboardRenderer::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    return SaveSnapshotPng(imagePath, snapshot, DashboardOverlayState{});
}

bool DashboardRenderer::SaveSnapshotPng(
    const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    if (!InitializeDirect2D()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    const UINT width = static_cast<UINT>(std::max(1, WindowWidth()));
    const UINT height = static_cast<UINT>(std::max(1, WindowHeight()));
    HRESULT hr = wicFactory_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr) || bitmap == nullptr) {
        lastError_ = "renderer:screenshot_wic_bitmap_failed hr=" + FormatHresult(hr);
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
        lastError_ = "renderer:screenshot_d2d_target_failed hr=" + FormatHresult(hr);
        return false;
    }

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get(), false)) {
        return false;
    }
    DrawDirect2DFrame(snapshot, overlayState);
    EndDirect2DDraw();
    if (!lastError_.empty()) {
        return false;
    }

    const bool saved = SaveWicBitmapPng(bitmap.Get(), imagePath);
    if (saved) {
        WriteScreenshotActiveRegionsTrace(overlayState);
    }
    return saved;
}

bool DashboardRenderer::PrimeLayoutEditDynamicRegions(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    if (!InitializeDirect2D()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    const UINT width = static_cast<UINT>(std::max(1, WindowWidth()));
    const UINT height = static_cast<UINT>(std::max(1, WindowHeight()));
    HRESULT hr = wicFactory_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr) || bitmap == nullptr) {
        lastError_ = "renderer:hover_wic_bitmap_failed hr=" + FormatHresult(hr);
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
        lastError_ = "renderer:hover_d2d_target_failed hr=" + FormatHresult(hr);
        return false;
    }

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get(), false)) {
        return false;
    }
    DrawDirect2DFrame(snapshot, overlayState);
    EndDirect2DDraw();
    return lastError_.empty();
}

void DashboardRenderer::WriteScreenshotActiveRegionsTrace(const DashboardOverlayState& overlayState) const {
    size_t count = 0;
    const auto appendRegion =
        [&](const RenderRect& box, std::string_view visualType, const std::string& path, const std::string& detail) {
            if (box.IsEmpty()) {
                return;
            }
            ++count;
            WriteTrace("diagnostics:active_region box=" + FormatTraceRect(box) +
                       " visual_type=" + Trace::QuoteText(visualType) + " path=" + Trace::QuoteText(path) +
                       " detail=" + Trace::QuoteText(detail));
        };

    if (overlayState.showLayoutEditGuides) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            const std::string cardPath =
                ActiveLayoutSectionName(config_) + ".cards/" + FormatNodePath(card.nodePath) + "/card[" + card.id + "]";
            appendRegion(card.rect, "card", cardPath, "card chrome " + card.id);
            if (card.chromeLayout.hasHeader) {
                appendRegion(
                    card.chromeLayout.titleRect, "card-header", cardPath + "/header", "card header " + card.id);
            }
            for (const auto& widget : card.widgets) {
                if (widget.widget == nullptr || !widget.widget->IsHoverable()) {
                    continue;
                }
                const std::string widgetType = std::string(EnumToString(widget.widget->Class()));
                appendRegion(widget.rect,
                    "widget-hover",
                    FormatLayoutConfigPath(config_, widget.editCardId, widget.nodePath) + "/widget[" + widgetType + "]",
                    "hoverable widget " + widgetType + " in card " + widget.cardId);
            }
        }

        for (const auto& guide : layoutResolver_->layoutEditGuides_) {
            appendRegion(guide.hitRect,
                "layout-weight-guide",
                FormatLayoutConfigPath(config_, guide.editCardId, guide.nodePath) + "/separator[" +
                    std::to_string(guide.separatorIndex) + "]",
                FormatGuideAxis(guide.axis) + " layout weight separator");
        }

        for (const auto& anchor : layoutResolver_->gapEditAnchors_) {
            appendRegion(anchor.hitRect,
                "gap-handle",
                FormatWidgetIdentityPath(config_, anchor.key.widget) + "/gap/" +
                    FormatLayoutConfigPath(config_, anchor.key.widget.editCardId, anchor.key.nodePath),
                FormatLayoutEditParameterDetail(anchor.key.parameter));
        }

        for (const auto& guide : layoutResolver_->widgetEditGuides_) {
            appendRegion(guide.hitRect,
                "widget-guide",
                FormatWidgetIdentityPath(config_, guide.widget) + "/guide[" + std::to_string(guide.guideId) + "]",
                FormatGuideAxis(guide.axis) + " " + FormatLayoutEditParameterDetail(guide.parameter));
        }

        const auto appendAnchorRegions = [&](const std::vector<LayoutEditAnchorRegion>& regions,
                                             std::string_view phase) {
            for (const auto& region : regions) {
                const std::string basePath = FormatWidgetIdentityPath(config_, region.key.widget) + "/anchor[" +
                                             std::to_string(region.key.anchorId) + "]";
                const std::string detail = std::string(phase) + " " + FormatAnchorShape(region.shape) + " " +
                                           FormatAnchorSubject(config_, region.key);
                appendRegion(region.anchorHitRect, "edit-anchor-handle", basePath + "/handle", detail);
                appendRegion(region.targetRect, "edit-anchor-target", basePath + "/target", detail);
            }
        };
        appendAnchorRegions(layoutResolver_->staticEditableAnchorRegions_, "static");
        appendAnchorRegions(layoutResolver_->dynamicEditableAnchorRegions_, "dynamic");

        const auto appendColorRegions = [&](const std::vector<LayoutEditColorRegion>& regions, std::string_view phase) {
            for (const auto& region : regions) {
                appendRegion(region.targetRect,
                    "color-target",
                    FormatLayoutEditParameterPath(region.parameter),
                    std::string(phase) + " color " + FormatLayoutEditParameterDetail(region.parameter));
            }
        };
        appendColorRegions(layoutResolver_->staticColorEditRegions_, "static");
        appendColorRegions(layoutResolver_->dynamicColorEditRegions_, "dynamic");
    }

    WriteTrace("diagnostics:active_regions count=" + std::to_string(count) +
               " layout_edit=" + Trace::BoolText(overlayState.showLayoutEditGuides));
}

bool DashboardRenderer::SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath) {
    if (wicFactory_ == nullptr || bitmap == nullptr) {
        lastError_ = "renderer:screenshot_wic_unavailable";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT hr = wicFactory_->CreateStream(stream.GetAddressOf());
    if (FAILED(hr) || stream == nullptr) {
        lastError_ = "renderer:screenshot_stream_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = stream->InitializeFromFilename(imagePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_stream_open_failed hr=" + FormatHresult(hr) + " path=\"" +
                     Utf8FromWide(imagePath.wstring()) + "\"";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr) || encoder == nullptr) {
        lastError_ = "renderer:screenshot_encoder_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_encoder_init_failed hr=" + FormatHresult(hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr) || frame == nullptr) {
        lastError_ = "renderer:screenshot_frame_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_init_failed hr=" + FormatHresult(hr);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = bitmap->GetSize(&width, &height);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_bitmap_size_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_size_failed hr=" + FormatHresult(hr);
        return false;
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_format_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = frame->WriteSource(bitmap, nullptr);
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_write_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_frame_commit_failed hr=" + FormatHresult(hr);
        return false;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        lastError_ = "renderer:screenshot_commit_failed hr=" + FormatHresult(hr);
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
    if (const std::optional<int> cachedWidth = textWidthCache_->Find(style, text)) {
        return *cachedWidth;
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
    textWidthCache_->Store(style, text, width);
    return width;
}

std::vector<LayoutGuideSnapCandidate> DashboardRenderer::CollectLayoutGuideSnapCandidates(
    const LayoutEditGuide& guide) const {
    struct SimilarityTypeKey {
        WidgetClass widgetClass = WidgetClass::Unknown;
        int extent = 0;

        bool operator<(const SimilarityTypeKey& other) const {
            if (widgetClass != other.widgetClass) {
                return widgetClass < other.widgetClass;
            }
            return extent < other.extent;
        }
    };

    std::vector<const WidgetLayout*> allWidgets = CollectSimilarityIndicatorWidgets(guide.axis);
    std::vector<const WidgetLayout*> affectedWidgets;
    for (const WidgetLayout* widget : allWidgets) {
        if (IsWidgetAffectedByGuide(*widget, guide)) {
            affectedWidgets.push_back(widget);
        }
    }

    std::vector<LayoutGuideSnapCandidate> candidates;
    for (const WidgetLayout* affected : affectedWidgets) {
        const int startExtent = WidgetExtentForAxis(*affected, guide.axis);
        if (startExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        std::set<SimilarityTypeKey> seenTargets;
        for (size_t i = 0; i < allWidgets.size(); ++i) {
            const WidgetLayout* target = allWidgets[i];
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
    const LayoutEditWidgetIdentity& identity, LayoutGuideAxis axis) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
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
    if (!ApplyGuideWeights(config_, target, weights)) {
        return false;
    }
    return ResolveLayout(false);
}

const MetricDefinitionConfig* DashboardRenderer::FindConfiguredMetricDefinition(std::string_view metricRef) const {
    const std::string key(metricRef);
    const auto cached = metricDefinitionCache_.find(key);
    if (cached != metricDefinitionCache_.end()) {
        return cached->second;
    }
    const MetricDefinitionConfig* definition = FindEffectiveMetricDefinition(config_.layout.metrics, metricRef);
    metricDefinitionCache_.emplace(key, definition);
    return definition;
}

const std::string& DashboardRenderer::ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const {
    const std::string key(metricRef);
    const auto cached = metricSampleValueTextCache_.find(key);
    if (cached != metricSampleValueTextCache_.end()) {
        return cached->second;
    }
    return metricSampleValueTextCache_.emplace(key, ResolveMetricSampleValueText(config_.layout.metrics, key))
        .first->second;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::HitTestLayoutCard(RenderPoint clientPoint) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (card.rect.Contains(clientPoint)) {
            return LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::HitTestEditableCard(RenderPoint clientPoint) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (!card.rect.Contains(clientPoint) || clientPoint.y > card.chromeLayout.contentRect.top) {
            continue;
        }
        return LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    }
    return std::nullopt;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::HitTestEditableWidget(RenderPoint clientPoint) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || !widget.rect.Contains(clientPoint)) {
                continue;
            }
            return LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditGapAnchorKey> DashboardRenderer::HitTestGapEditAnchor(RenderPoint clientPoint) const {
    const LayoutEditGapAnchor* bestAnchor = nullptr;
    int bestPriority = 0;
    for (auto it = layoutResolver_->gapEditAnchors_.rbegin(); it != layoutResolver_->gapEditAnchors_.rend(); ++it) {
        if (!it->hitRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority(it->key.parameter);
        if (bestAnchor == nullptr || priority < bestPriority) {
            bestAnchor = &(*it);
            bestPriority = priority;
        }
    }
    return bestAnchor != nullptr ? std::optional<LayoutEditGapAnchorKey>(bestAnchor->key) : std::nullopt;
}

std::optional<LayoutEditAnchorKey> DashboardRenderer::HitTestEditableAnchorTarget(RenderPoint clientPoint) const {
    std::vector<const LayoutEditAnchorRegion*> regions;
    regions.reserve(
        layoutResolver_->staticEditableAnchorRegions_.size() + layoutResolver_->dynamicEditableAnchorRegions_.size());
    for (const auto& region : layoutResolver_->staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : layoutResolver_->dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!(*it)->targetRect.Contains(clientPoint)) {
            continue;
        }

        const long long width = (std::max<LONG>)(0, (*it)->targetRect.right - (*it)->targetRect.left);
        const long long height = (std::max<LONG>)(0, (*it)->targetRect.bottom - (*it)->targetRect.top);
        const long long area = width * height;
        if (bestRegion == nullptr || area < bestArea) {
            bestRegion = *it;
            bestArea = area;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditAnchorKey>(bestRegion->key) : std::nullopt;
}

std::optional<LayoutEditAnchorKey> DashboardRenderer::HitTestEditableAnchorHandle(RenderPoint clientPoint) const {
    std::vector<const LayoutEditAnchorRegion*> regions;
    regions.reserve(
        layoutResolver_->staticEditableAnchorRegions_.size() + layoutResolver_->dynamicEditableAnchorRegions_.size());
    for (const auto& region : layoutResolver_->staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : layoutResolver_->dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    int bestPriority = 0;
    const auto hoverPriority = [](const LayoutEditAnchorRegion& region) {
        if (region.shape == AnchorShape::Square) {
            return -2;
        }
        if (region.shape == AnchorShape::Wedge) {
            return 2;
        }
        return LayoutEditAnchorHitPriority(region.key);
    };
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        const LayoutEditAnchorRegion& region = *(*it);
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

        const int priority = hoverPriority(region);
        if (bestRegion == nullptr || priority < bestPriority) {
            bestRegion = &region;
            bestPriority = priority;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditAnchorKey>(bestRegion->key) : std::nullopt;
}

std::optional<LayoutEditAnchorRegion> DashboardRenderer::FindEditableAnchorRegion(
    const LayoutEditAnchorKey& key) const {
    const auto findIn =
        [&](const std::vector<LayoutEditAnchorRegion>& regions) -> std::optional<LayoutEditAnchorRegion> {
        const auto it = std::find_if(regions.begin(), regions.end(), [&](const LayoutEditAnchorRegion& region) {
            return MatchesEditableAnchorKey(region.key, key);
        });
        if (it == regions.end()) {
            return std::nullopt;
        }
        return *it;
    };
    if (const auto staticRegion = findIn(layoutResolver_->staticEditableAnchorRegions_); staticRegion.has_value()) {
        return staticRegion;
    }
    return findIn(layoutResolver_->dynamicEditableAnchorRegions_);
}

std::optional<LayoutEditColorRegion> DashboardRenderer::HitTestEditableColorRegion(RenderPoint clientPoint) const {
    std::vector<const LayoutEditColorRegion*> regions;
    regions.reserve(layoutResolver_->staticColorEditRegions_.size() + layoutResolver_->dynamicColorEditRegions_.size());
    for (const auto& region : layoutResolver_->staticColorEditRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : layoutResolver_->dynamicColorEditRegions_) {
        regions.push_back(&region);
    }
    const LayoutEditColorRegion* bestRegion = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!(*it)->targetRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority((*it)->parameter);
        const long long width = (std::max<LONG>)(0, (*it)->targetRect.right - (*it)->targetRect.left);
        const long long height = (std::max<LONG>)(0, (*it)->targetRect.bottom - (*it)->targetRect.top);
        const long long area = width * height;
        if (bestRegion == nullptr || priority < bestPriority || (priority == bestPriority && area < bestArea)) {
            bestRegion = *it;
            bestPriority = priority;
            bestArea = area;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditColorRegion>(*bestRegion) : std::nullopt;
}

std::optional<LayoutEditGapAnchor> DashboardRenderer::FindGapEditAnchor(const LayoutEditGapAnchorKey& key) const {
    const auto it = std::find_if(layoutResolver_->gapEditAnchors_.begin(),
        layoutResolver_->gapEditAnchors_.end(),
        [&](const LayoutEditGapAnchor& anchor) { return MatchesGapEditAnchorKey(anchor.key, key); });
    if (it == layoutResolver_->gapEditAnchors_.end()) {
        return std::nullopt;
    }
    return *it;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::FindFirstLayoutEditPreviewWidget(
    const std::string& widgetTypeName) const {
    const std::string normalizedName = ToLower(Trim(widgetTypeName));
    const auto widgetClass = normalizedName.empty() ? std::nullopt : EnumFromString<WidgetClass>(normalizedName);
    if (!widgetClass.has_value()) {
        return std::nullopt;
    }

    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || widget.widget->Class() != *widgetClass) {
                continue;
            }
            return LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

bool DashboardRenderer::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    lastError_.clear();
    RebuildPalette();
    if (!InitializeDirect2D() || !LoadIcons()) {
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
    layoutResolver_->Clear();
    textWidthCache_->Clear();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    d2dFirstDrawWarmupPending_ = false;
    d2dCache_->Clear();
    ReleaseIcons();
    ShutdownDirect2D();
}

void DashboardRenderer::DiscardWindowRenderTarget(std::string_view reason) {
    if (!reason.empty()) {
        WriteTrace("renderer:d2d_window_target_discard reason=\"" + std::string(reason) + "\"");
    }
    d2dClipDepth_ = 0;
    if (d2dActiveRenderTarget_ == d2dWindowRenderTarget_.Get()) {
        d2dActiveRenderTarget_ = nullptr;
    }
    d2dWindowRenderTarget_.Reset();
    d2dCache_->ResetTarget();
}

const MetricSource& DashboardRenderer::ResolveMetrics(const SystemSnapshot& snapshot) {
    if (cachedMetricSource_ == nullptr || cachedMetricSnapshot_ != &snapshot ||
        cachedMetricSnapshotRevision_ != snapshot.revision) {
        cachedMetricSource_ = std::make_unique<MetricSource>(snapshot, config_.layout.metrics);
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
            lastError_ = "renderer:d2d_factory_failed hr=" + FormatHresult(hr);
            return false;
        }
    }
    if (dwriteFactory_ == nullptr) {
        const HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || dwriteFactory_ == nullptr) {
            lastError_ = "renderer:dwrite_factory_failed hr=" + FormatHresult(hr);
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
        lastError_ = "renderer:wic_com_init_failed hr=" + FormatHresult(initHr);
        return false;
    }
    wicComInitialized_ = initHr == S_OK || initHr == S_FALSE;

    const HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || wicFactory_ == nullptr) {
        lastError_ = "renderer:wic_factory_failed hr=" + FormatHresult(hr);
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
    d2dCache_->ResetTarget();
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
        const D2D1_PRESENT_OPTIONS presentOptions =
            d2dImmediatePresent_ ? D2D1_PRESENT_OPTIONS_IMMEDIATELY : D2D1_PRESENT_OPTIONS_NONE;
        const HRESULT hr = d2dFactory_->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(width, height), presentOptions),
            d2dWindowRenderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || d2dWindowRenderTarget_ == nullptr) {
            lastError_ = "renderer:d2d_hwnd_target_failed hr=" + FormatHresult(hr);
            return false;
        }
        d2dCache_->ResetTarget();
        return true;
    }

    const D2D1_SIZE_U currentSize = d2dWindowRenderTarget_->GetPixelSize();
    if (currentSize.width == width && currentSize.height == height) {
        return true;
    }
    const HRESULT hr = d2dWindowRenderTarget_->Resize(D2D1::SizeU(width, height));
    if (FAILED(hr)) {
        d2dWindowRenderTarget_.Reset();
        d2dCache_->ResetTarget();
        return EnsureWindowRenderTarget();
    }
    d2dCache_->Clear();
    return true;
}

bool DashboardRenderer::BeginDirect2DDraw(ID2D1RenderTarget* target, bool allowDeferredWarmup) {
    if (target == nullptr) {
        lastError_ = "renderer:d2d_target_missing";
        return false;
    }
    if (allowDeferredWarmup && d2dFirstDrawWarmupPending_) {
        d2dFirstDrawWarmupPending_ = false;
        WriteTrace("renderer:d2d_warmup_begin");
        if (!RebuildTextFormatsAndMetrics() || !ResolveLayout()) {
            WriteTrace("renderer:d2d_warmup_failed");
            d2dFirstDrawWarmupPending_ = true;
            return false;
        }
        WriteTrace("renderer:d2d_warmup_done");
    }
    d2dCache_->AttachTarget(target);
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
    if (!activeWindowTarget) {
        d2dCache_->ResetTarget();
    }
    if (activeWindowTarget && hr == D2DERR_RECREATE_TARGET) {
        DiscardWindowRenderTarget("recreate_target");
    } else if (FAILED(hr)) {
        if (activeWindowTarget) {
            DiscardWindowRenderTarget("end_draw_failed");
        }
        lastError_ = "renderer:d2d_end_draw_failed hr=" + FormatHresult(hr);
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

ID2D1SolidColorBrush* DashboardRenderer::D2DSolidBrush(RenderColorId colorId) {
    if (d2dActiveRenderTarget_ == nullptr) {
        return nullptr;
    }
    return d2dCache_->SolidBrush(d2dActiveRenderTarget_, palette_->Get(colorId));
}

void DashboardRenderer::PushClipRect(const RenderRect& rect) {
    if (IsDrawActive()) {
        d2dActiveRenderTarget_->PushAxisAlignedClip(D2DRectFromRenderRect(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++d2dClipDepth_;
    }
}

void DashboardRenderer::PopClipRect() {
    if (IsDrawActive() && d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
}

bool DashboardRenderer::DrawIcon(std::string_view iconName, const RenderRect& rect) {
    if (!IsDrawActive() || iconName.empty() || rect.IsEmpty()) {
        return false;
    }
    d2dCache_->DrawIcon(wicFactory_.Get(), d2dActiveRenderTarget_, icons_, iconName, rect);
    return true;
}

bool DashboardRenderer::FillSolidRect(const RenderRect& rect, RenderColorId color) {
    if (!IsDrawActive() || rect.IsEmpty()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->FillRectangle(D2DRectFromRenderRect(rect), brush);
    return true;
}

bool DashboardRenderer::FillSolidRoundedRect(const RenderRect& rect, int radius, RenderColorId color) {
    if (!IsDrawActive() || rect.IsEmpty()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    const float clampedRadius = static_cast<float>(std::max(0, radius));
    d2dActiveRenderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(D2DRectFromRenderRect(rect), clampedRadius, clampedRadius), brush);
    return true;
}

bool DashboardRenderer::FillSolidEllipse(const RenderRect& rect, RenderColorId color) {
    if (!IsDrawActive() || rect.IsEmpty()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(color);
    if (brush == nullptr) {
        return false;
    }
    const float radiusX = static_cast<float>(rect.Width()) / 2.0f;
    const float radiusY = static_cast<float>(rect.Height()) / 2.0f;
    d2dActiveRenderTarget_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(static_cast<float>(rect.left) + radiusX, static_cast<float>(rect.top) + radiusY),
            radiusX,
            radiusY),
        brush);
    return true;
}

bool DashboardRenderer::FillSolidDiamond(const RenderRect& rect, RenderColorId color) {
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
    d2dActiveRenderTarget_->DrawRectangle(D2DRectFromRenderRect(rect),
        brush,
        (std::max)(1.0f, stroke.width),
        stroke.pattern == StrokePattern::Dotted ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::DrawSolidRoundedRect(const RenderRect& rect, int radius, const RenderStroke& stroke) {
    if (!IsDrawActive()) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(stroke.color);
    if (brush == nullptr) {
        return false;
    }
    const float clampedRadius = static_cast<float>(std::max(0, radius));
    d2dActiveRenderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2DRectFromRenderRect(rect), clampedRadius, clampedRadius),
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
    d2dActiveRenderTarget_->DrawEllipse(D2D1::Ellipse(D2DPointFromRenderPoint(rect.Center()), radiusX, radiusY),
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
    d2dActiveRenderTarget_->DrawLine(D2DPointFromRenderPoint(start),
        D2DPointFromRenderPoint(end),
        brush,
        (std::max)(1.0f, stroke.width),
        stroke.pattern == StrokePattern::Dotted ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::DrawArc(const RenderArc& arc, const RenderStroke& stroke) {
    if (!IsDrawActive()) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderArcGeometry(d2dFactory_.Get(), arc);
    return DrawD2DGeometry(geometry.Get(), stroke);
}

bool DashboardRenderer::DrawArcs(std::span<const RenderArc> arcs, const RenderStroke& stroke) {
    if (!IsDrawActive() || arcs.empty()) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderArcsGeometry(d2dFactory_.Get(), arcs);
    return DrawD2DGeometry(geometry.Get(), stroke);
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

bool DashboardRenderer::FillD2DGeometry(ID2D1Geometry* geometry, RenderColorId color) {
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

bool DashboardRenderer::DrawD2DGeometry(ID2D1Geometry* geometry, const RenderStroke& stroke) {
    if (!IsDrawActive() || geometry == nullptr) {
        return false;
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(stroke.color);
    if (brush == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawGeometry(geometry,
        brush,
        (std::max)(1.0f, stroke.width),
        stroke.pattern == StrokePattern::Dotted ? d2dDashedStrokeStyle_.Get() : d2dSolidStrokeStyle_.Get());
    return true;
}

bool DashboardRenderer::FillPath(const RenderPath& path, RenderColorId color) {
    if (!IsDrawActive() || path.IsEmpty()) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderPathGeometry(d2dFactory_.Get(), path);
    return FillD2DGeometry(geometry.Get(), color);
}

bool DashboardRenderer::FillPaths(std::span<const RenderPath> paths, RenderColorId color) {
    if (!IsDrawActive() || paths.empty()) {
        return false;
    }

    std::vector<Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometries;
    geometries.reserve(paths.size());
    for (const RenderPath& path : paths) {
        if (path.IsEmpty()) {
            continue;
        }
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderPathGeometry(d2dFactory_.Get(), path);
        if (geometry != nullptr) {
            geometries.push_back(std::move(geometry));
        }
    }
    if (geometries.empty()) {
        return false;
    }
    if (geometries.size() == 1) {
        return FillD2DGeometry(geometries.front().Get(), color);
    }
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> group = CreateD2DGeometryGroup(geometries, geometries.size());
    return FillD2DGeometry(group.Get(), color);
}

bool DashboardRenderer::DrawPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke) {
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
    sink->BeginFigure(D2DPointFromRenderPoint(points.front()), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(D2DPointFromRenderPoint(points[i]));
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
    palette_->Rebuild(config_.layout.colors);
}

bool DashboardRenderer::LoadIcons() {
    ReleaseIcons();
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
        const UINT resourceId = GetIconResourceId(iconName);
        if (resourceId == 0) {
            lastError_ = "renderer:icon_unknown name=\"" + iconName + "\"";
            ReleaseIcons();
            return false;
        }
        auto bitmap = LoadPngResourceBitmap(wicFactory_.Get(), resourceId);
        if (bitmap == nullptr) {
            lastError_ = "renderer:icon_load_failed name=\"" + iconName + "\" resource=" + std::to_string(resourceId);
            ReleaseIcons();
            return false;
        }
        auto tintedBitmap =
            TintMonochromeBitmapSource(wicFactory_.Get(), bitmap.Get(), palette_->Get(RenderColorId::Icon));
        if (tintedBitmap == nullptr) {
            lastError_ = "renderer:icon_tint_failed name=\"" + iconName + "\" resource=" + std::to_string(resourceId);
            ReleaseIcons();
            return false;
        }
        icons_.push_back({iconName, std::move(tintedBitmap)});
    }
    return true;
}

void DashboardRenderer::ReleaseIcons() {
    d2dCache_->ClearIconBitmaps();
    icons_.clear();
}

void DashboardRenderer::WriteTrace(const std::string& text) const {
    if (interactiveDragTraceActive_ && text.rfind("renderer:", 0) == 0) {
        return;
    }
    trace_.Write(text);
}

bool DashboardRenderer::RebuildTextFormatsAndMetrics() {
    if (!CreateDWriteTextFormats()) {
        lastError_ = "renderer:text_format_create_failed";
        return false;
    }
    textWidthCache_->Clear();
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

bool DashboardRenderer::SupportsLayoutSimilarityIndicator(const WidgetLayout& widget) const {
    if (widget.widget == nullptr || widget.widget->IsVerticalSpring()) {
        return false;
    }
    if (UsesFixedPreferredHeightInRows(widget)) {
        return false;
    }
    return true;
}

std::vector<const WidgetLayout*> DashboardRenderer::CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const {
    struct SimilarityRepresentativeKey {
        std::string cardId;
        WidgetClass widgetClass = WidgetClass::Unknown;
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

    std::vector<const WidgetLayout*> widgets;
    std::unordered_set<SimilarityRepresentativeKey, SimilarityRepresentativeKeyHash> seenKeys;
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
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

bool DashboardRenderer::IsContainerNode(const LayoutNodeConfig& node) {
    return node.name == "rows" || node.name == "columns";
}

bool DashboardRenderer::UsesFixedPreferredHeightInRows(const WidgetLayout& widget) const {
    return widget.widget != nullptr && widget.widget->UsesFixedPreferredHeightInRows();
}

const LayoutCardConfig* DashboardRenderer::FindCardConfigById(const std::string& id) const {
    const auto it = std::find_if(
        config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) { return card.id == id; });
    return it != config_.layout.cards.end() ? &(*it) : nullptr;
}
