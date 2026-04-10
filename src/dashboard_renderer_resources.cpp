#include "dashboard_renderer.h"

#include <algorithm>
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

#include <gdiplus.h>

#include "../resources/resource.h"
#include "trace.h"
#include "utf8.h"

namespace {

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

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

std::string FormatRect(const RECT& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) +
           "," + std::to_string(rect.bottom) + ")";
}

SIZE MeasureTextSize(HDC hdc, HFONT font, const std::string& text) {
    SIZE size{};
    const std::wstring wide = WideFromUtf8(text);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    if (!wide.empty()) {
        GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), &size);
    }
    SelectObject(hdc, oldFont);
    return size;
}

}  // namespace

DashboardRenderer::DashboardRenderer() = default;

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

void DashboardRenderer::SetConfig(const AppConfig& config) {
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
        }
    }
}

void DashboardRenderer::SetRenderMode(RenderMode mode) {
    renderMode_ = mode;
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

const DashboardRenderer::MeasuredWidths& DashboardRenderer::MeasuredTextWidths() const {
    return measuredWidths_;
}

const DashboardRenderer::Fonts& DashboardRenderer::WidgetFonts() const {
    return fonts_;
}

DashboardRenderer::RenderMode DashboardRenderer::CurrentRenderMode() const {
    return renderMode_;
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
    for (auto it = editableAnchorRegions_.rbegin(); it != editableAnchorRegions_.rend(); ++it) {
        if (PtInRect(&it->targetRect, clientPoint)) {
            return it->key;
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorKey> DashboardRenderer::HitTestEditableAnchorHandle(
    POINT clientPoint) const {
    for (auto it = editableAnchorRegions_.rbegin(); it != editableAnchorRegions_.rend(); ++it) {
        bool hit = false;
        if (it->shape == AnchorShape::Circle) {
            const int width = std::max(1L, it->anchorRect.right - it->anchorRect.left);
            const int height = std::max(1L, it->anchorRect.bottom - it->anchorRect.top);
            const double radius = static_cast<double>(std::max(width, height)) / 2.0;
            const double centerX = static_cast<double>(it->anchorRect.left) + static_cast<double>(width) / 2.0;
            const double centerY = static_cast<double>(it->anchorRect.top) + static_cast<double>(height) / 2.0;
            const double dx = static_cast<double>(clientPoint.x) - centerX;
            const double dy = static_cast<double>(clientPoint.y) - centerY;
            const double distance = std::sqrt((dx * dx) + (dy * dy));
            hit = std::abs(distance - radius) <= static_cast<double>(it->anchorHitPadding);
        } else {
            hit = PtInRect(&it->anchorHitRect, clientPoint);
        }
        if (hit) {
            return it->key;
        }
    }
    return std::nullopt;
}

std::optional<DashboardRenderer::EditableAnchorRegion> DashboardRenderer::FindEditableAnchorRegion(
    const EditableAnchorKey& key) const {
    const auto it = std::find_if(editableAnchorRegions_.begin(),
        editableAnchorRegions_.end(),
        [&](const EditableAnchorRegion& region) { return MatchesEditableAnchorKey(region.key, key); });
    if (it == editableAnchorRegions_.end()) {
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
    if (!InitializeGdiplus() || !LoadPanelIcons()) {
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
    return true;
}

void DashboardRenderer::Shutdown() {
    DestroyFonts();
    fontHeights_ = {};
    measuredWidths_ = {};
    resolvedLayout_ = {};
    parsedWidgetInfoCache_.clear();
    editableAnchorRegions_.clear();
    ReleasePanelIcons();
    ShutdownGdiplus();
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
    return fonts_.title != nullptr && fonts_.big != nullptr && fonts_.value != nullptr && fonts_.label != nullptr &&
           fonts_.text != nullptr && fonts_.smallFont != nullptr && fonts_.footer != nullptr &&
           fonts_.clockTime != nullptr && fonts_.clockDate != nullptr;
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
    panelIcons_.clear();
}

void DashboardRenderer::WriteTrace(const std::string& text) const {
    if (traceOutput_ == nullptr) {
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
    measuredWidths_.throughputAxis = MeasureTextSize(hdc, fonts_.smallFont, "1000").cx +
                                     std::max(0, ScaleLogical(config_.layout.throughput.axisPadding));
    measuredWidths_.driveLabel = MeasureTextSize(hdc, fonts_.label, "W:").cx;
    measuredWidths_.drivePercent = MeasureTextSize(hdc, fonts_.label, "100%").cx;
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    WriteTrace("renderer:font_metrics title=" + std::to_string(fontHeights_.title) +
               " big=" + std::to_string(fontHeights_.big) + " value=" + std::to_string(fontHeights_.value) +
               " label=" + std::to_string(fontHeights_.label) + " text=" + std::to_string(fontHeights_.text) +
               " small=" + std::to_string(fontHeights_.smallText) + " footer=" + std::to_string(fontHeights_.footer) +
               " clock_time=" + std::to_string(fontHeights_.clockTime) + " clock_date=" +
               std::to_string(fontHeights_.clockDate) + " render_scale=" + std::to_string(renderScale_) +
               " throughput_axis_width=" + std::to_string(measuredWidths_.throughputAxis) +
               " drive_label_width=" + std::to_string(measuredWidths_.driveLabel) +
               " drive_percent_width=" + std::to_string(measuredWidths_.drivePercent));
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

bool DashboardRenderer::IsFirstWidgetForSimilarityIndicator(
    const DashboardWidgetLayout& widget, LayoutGuideAxis axis) const {
    const int extent = WidgetExtentForAxis(widget, axis);
    if (extent <= 0) {
        return false;
    }

    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& candidate : card.widgets) {
            if (&candidate == &widget || candidate.cardId != widget.cardId || candidate.widget == nullptr ||
                widget.widget == nullptr || candidate.widget->Class() != widget.widget->Class()) {
                continue;
            }
            if (!SupportsLayoutSimilarityIndicator(candidate) || WidgetExtentForAxis(candidate, axis) != extent) {
                continue;
            }

            if (axis == LayoutGuideAxis::Vertical) {
                if (candidate.rect.left == widget.rect.left && candidate.rect.right == widget.rect.right &&
                    candidate.rect.top < widget.rect.top) {
                    return false;
                }
            } else {
                if (candidate.rect.top == widget.rect.top && candidate.rect.bottom == widget.rect.bottom &&
                    candidate.rect.left < widget.rect.left) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<const DashboardWidgetLayout*> DashboardRenderer::CollectSimilarityIndicatorWidgets(
    LayoutGuideAxis axis) const {
    std::vector<const DashboardWidgetLayout*> widgets;
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (!SupportsLayoutSimilarityIndicator(widget) || !IsFirstWidgetForSimilarityIndicator(widget, axis)) {
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

DashboardWidgetLayout DashboardRenderer::ResolveWidgetLayout(const LayoutNodeConfig& node, const RECT& rect) const {
    DashboardWidgetLayout widget;
    widget.rect = rect;
    const ParsedWidgetInfo* info = FindParsedWidgetInfo(node);
    if (info != nullptr) {
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
