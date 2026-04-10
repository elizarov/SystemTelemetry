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
#include <sstream>

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

bool ContainsCardReference(const std::vector<std::string>& stack, const std::string& cardId) {
    return std::find(stack.begin(), stack.end(), cardId) != stack.end();
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

POINT PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{cx + static_cast<LONG>(std::round(std::cos(radians) * radius)),
        cy - static_cast<LONG>(std::round(std::sin(radians) * radius))};
}

Gdiplus::PointF GaugePoint(float cx, float cy, float radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return Gdiplus::PointF(cx + static_cast<Gdiplus::REAL>(std::cos(radians) * radius),
        cy + static_cast<Gdiplus::REAL>(std::sin(radians) * radius));
}

void AddCapsulePath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect) {
    const float width = std::max(0.0f, rect.Width);
    const float height = std::max(0.0f, rect.Height);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float diameter = std::max(1.0f, std::min(width, height));
    const float centerWidth = std::max(0.0f, width - diameter);
    const float rightArcLeft = rect.X + centerWidth;

    path.StartFigure();
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rightArcLeft, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rightArcLeft, rect.Y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillGaugeSegment(Gdiplus::Graphics& graphics,
    float cx,
    float cy,
    float radius,
    float thickness,
    double startAngleDegrees,
    double sweepAngleDegrees,
    const Gdiplus::Color& color) {
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
    path.AddArc(
        outerRect, static_cast<Gdiplus::REAL>(startAngleDegrees), static_cast<Gdiplus::REAL>(sweepAngleDegrees));
    path.AddLine(outerEnd, innerEnd);
    if (innerRadius > 0.0f) {
        path.AddArc(innerRect,
            static_cast<Gdiplus::REAL>(startAngleDegrees + sweepAngleDegrees),
            static_cast<Gdiplus::REAL>(-sweepAngleDegrees));
    } else {
        path.AddLine(innerEnd, Gdiplus::PointF(cx, cy));
    }
    path.AddLine(innerStart, outerStart);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
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

void DrawSegmentIndicator(HDC hdc,
    const RECT& rect,
    int segmentCount,
    int segmentGap,
    double ratio,
    COLORREF trackColor,
    COLORREF accentColor) {
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
    const int filledSegments =
        clampedRatio > 0.0
            ? std::clamp(static_cast<int>(std::ceil(clampedRatio * static_cast<double>(segmentCount))), 1, segmentCount)
            : 0;
    int top = rect.top;
    for (int index = segmentCount - 1; index >= 0; --index) {
        const int extra = (segmentCount - 1 - index) < remainder ? 1 : 0;
        const int segmentHeight = baseSegmentHeight + extra;
        const int visualHeight = std::min(segmentHeight, std::max(2, width / 2));
        const int segmentTop = top + std::max(0, (segmentHeight - visualHeight) / 2);
        RECT segmentRect{
            rect.left, segmentTop, rect.right, std::min(rect.bottom, static_cast<LONG>(segmentTop + visualHeight))};
        HBRUSH trackBrush = CreateSolidBrush(trackColor);
        FillRect(hdc, &segmentRect, trackBrush);
        DeleteObject(trackBrush);

        if (index < filledSegments) {
            RECT fillRect = segmentRect;
            HBRUSH fillBrush = CreateSolidBrush(accentColor);
            FillRect(hdc, &fillRect, fillBrush);
            DeleteObject(fillBrush);
        }

        top = segmentRect.bottom + std::max(0, segmentGap);
    }
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
        WidgetKind kind = WidgetKind::Unknown;
        int extent = 0;

        bool operator<(const SimilarityTypeKey& other) const {
            if (kind != other.kind) {
                return kind < other.kind;
            }
            return extent < other.extent;
        }
    };

    std::vector<const ResolvedWidgetLayout*> allWidgets = CollectSimilarityIndicatorWidgets(guide.axis);
    std::vector<const ResolvedWidgetLayout*> affectedWidgets;
    for (const ResolvedWidgetLayout* widget : allWidgets) {
        if (IsWidgetAffectedByGuide(*widget, guide)) {
            affectedWidgets.push_back(widget);
        }
    }

    std::vector<LayoutGuideSnapCandidate> candidates;
    for (const ResolvedWidgetLayout* affected : affectedWidgets) {
        const int startExtent = WidgetExtentForAxis(*affected, guide.axis);
        if (startExtent <= 0) {
            continue;
        }
        std::set<SimilarityTypeKey> seenTargets;
        for (size_t i = 0; i < allWidgets.size(); ++i) {
            const ResolvedWidgetLayout* target = allWidgets[i];
            if (target == affected || target->kind != affected->kind) {
                continue;
            }
            const SimilarityTypeKey typeKey{target->kind, WidgetExtentForAxis(*target, guide.axis)};
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
            const bool hoverableWidget = widget.kind != WidgetKind::Spacer &&
                                         widget.kind != WidgetKind::VerticalSpring &&
                                         widget.kind != WidgetKind::Unknown;
            if (!hoverableWidget || !PtInRect(&widget.rect, clientPoint)) {
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
        if (PtInRect(&it->anchorHitRect, clientPoint)) {
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
    auto matchesType = [&](WidgetKind kind) {
        switch (kind) {
            case WidgetKind::Text:
                return normalizedName == "text";
            case WidgetKind::Gauge:
                return normalizedName == "gauge";
            case WidgetKind::MetricList:
                return normalizedName == "metric_list";
            case WidgetKind::Throughput:
                return normalizedName == "throughput";
            case WidgetKind::NetworkFooter:
                return normalizedName == "network_footer";
            case WidgetKind::DriveUsageList:
                return normalizedName == "drive_usage_list";
            case WidgetKind::ClockTime:
                return normalizedName == "clock_time";
            case WidgetKind::ClockDate:
                return normalizedName == "clock_date";
            default:
                return false;
        }
    };

    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            const bool hoverableWidget = widget.kind != WidgetKind::Spacer &&
                                         widget.kind != WidgetKind::VerticalSpring &&
                                         widget.kind != WidgetKind::Unknown;
            if (!hoverableWidget || !matchesType(widget.kind)) {
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
    measuredWidths_.throughputLabel =
        std::max(MeasureTextSize(hdc, fonts_.smallFont, "Read").cx, MeasureTextSize(hdc, fonts_.smallFont, "Write").cx);
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
               " throughput_label_width=" + std::to_string(measuredWidths_.throughputLabel) +
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

int DashboardRenderer::EffectiveMetricRowHeight() const {
    const int textHeight = std::max(fontHeights_.label, fontHeights_.value);
    const int barHeight = std::max(1, ScaleLogical(config_.layout.metricList.barHeight));
    const int verticalGap = std::max(0, ScaleLogical(config_.layout.metricList.verticalGap));
    const int computed = textHeight + verticalGap + barHeight;
    WriteTrace("renderer:layout_metric_row_height text=" + std::to_string(textHeight) +
               " bar=" + std::to_string(barHeight) + " gap=" + std::to_string(verticalGap) +
               " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveDriveHeaderHeight() const {
    const int headerGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.headerGap));
    const int computed = fontHeights_.smallText + headerGap;
    WriteTrace("renderer:layout_drive_header_height text=" + std::to_string(fontHeights_.smallText) +
               " gap=" + std::to_string(headerGap) + " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveDriveRowHeight() const {
    const int textHeight = std::max(fontHeights_.label, fontHeights_.smallText);
    const int barHeight = std::max(1, ScaleLogical(config_.layout.driveUsageList.barHeight));
    const int rowGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.rowGap));
    const int computed = std::max(textHeight, barHeight) + rowGap;
    WriteTrace("renderer:layout_drive_row_height text=" + std::to_string(textHeight) + " bar=" +
               std::to_string(barHeight) + " gap=" + std::to_string(rowGap) + " effective=" + std::to_string(computed));
    return computed;
}

bool DashboardRenderer::SupportsLayoutSimilarityIndicator(const ResolvedWidgetLayout& widget) const {
    if (widget.kind == WidgetKind::VerticalSpring) {
        return false;
    }
    if (UsesFixedPreferredHeightInRows(widget)) {
        return false;
    }
    return widget.kind != WidgetKind::Unknown;
}

bool DashboardRenderer::IsFirstWidgetForSimilarityIndicator(
    const ResolvedWidgetLayout& widget, LayoutGuideAxis axis) const {
    const int extent = WidgetExtentForAxis(widget, axis);
    if (extent <= 0) {
        return false;
    }

    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& candidate : card.widgets) {
            if (&candidate == &widget || candidate.cardId != widget.cardId || candidate.kind != widget.kind) {
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

std::vector<const DashboardRenderer::ResolvedWidgetLayout*> DashboardRenderer::CollectSimilarityIndicatorWidgets(
    LayoutGuideAxis axis) const {
    std::vector<const ResolvedWidgetLayout*> widgets;
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
    if (node.name == "text") {
        const int height = fontHeights_.text + std::max(0, ScaleLogical(config_.layout.text.preferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "network_footer") {
        const int height =
            fontHeights_.footer + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "spacer") {
        const int height =
            fontHeights_.footer + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "vertical_spring") {
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=0");
        return 0;
    }
    if (node.name == "metric_list") {
        const std::string param = node.parameter;
        const int count = static_cast<int>(Split(param, ',').size());
        const int height = count * EffectiveMetricRowHeight();
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" rows=" + std::to_string(count) +
                   " value=" + std::to_string(height));
        return height;
    }
    if (node.name == "drive_usage_list") {
        const int count = static_cast<int>(config_.storage.drives.size());
        const int height = (count > 0 ? EffectiveDriveHeaderHeight() : 0) + (count * EffectiveDriveRowHeight());
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" rows=" + std::to_string(count) +
                   " value=" + std::to_string(height));
        return height;
    }
    if (node.name == "throughput") {
        const int height = EffectiveThroughputPreferredHeight();
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "clock_time") {
        const int height = fontHeights_.clockTime;
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "clock_date") {
        const int height = fontHeights_.clockDate;
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "gauge") {
        const int height = std::max(1, ScaleLogical(config_.layout.gauge.preferredSize));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=0");
    return 0;
}

bool DashboardRenderer::IsContainerNode(const LayoutNodeConfig& node) {
    return node.name == "rows" || node.name == "columns";
}

int DashboardRenderer::EffectiveThroughputPreferredHeight() const {
    const int headerHeight = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.throughput.valuePadding));
    const int graphLabelHeight =
        std::max(fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.throughput.scaleLabelPadding)),
            std::max(1, ScaleLogical(config_.layout.throughput.scaleLabelMinHeight)));
    return headerHeight + std::max(0, ScaleLogical(config_.layout.throughput.headerGap)) + graphLabelHeight;
}

int DashboardRenderer::GaugeRadiusForRect(const RECT& rect) const {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    const int outerPadding = std::max(0, ScaleLogical(config_.layout.gauge.outerPadding));
    const int fittedRadius = std::max(1, std::min(width, height) / 2 - outerPadding);
    return fittedRadius;
}

DashboardRenderer::ResolvedWidgetLayout DashboardRenderer::ResolveWidgetLayout(
    const LayoutNodeConfig& node, const RECT& rect) const {
    ResolvedWidgetLayout widget;
    widget.rect = rect;
    if (node.name == "text") {
        widget.kind = WidgetKind::Text;
        widget.binding.metric = node.parameter;
        widget.preferredHeight = fontHeights_.text + std::max(0, ScaleLogical(config_.layout.text.preferredPadding));
        widget.fixedPreferredHeightInRows = true;
    } else if (node.name == "gauge") {
        widget.kind = WidgetKind::Gauge;
        widget.binding.metric = node.parameter;
        widget.preferredHeight = std::max(1, ScaleLogical(config_.layout.gauge.preferredSize));
    } else if (node.name == "metric_list") {
        widget.kind = WidgetKind::MetricList;
        widget.binding.param = node.parameter;
        widget.preferredHeight = static_cast<int>(Split(node.parameter, ',').size()) * EffectiveMetricRowHeight();
    } else if (node.name == "throughput") {
        widget.kind = WidgetKind::Throughput;
        widget.binding.metric = node.parameter;
        widget.preferredHeight = EffectiveThroughputPreferredHeight();
    } else if (node.name == "network_footer") {
        widget.kind = WidgetKind::NetworkFooter;
        widget.preferredHeight =
            fontHeights_.footer + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        widget.fixedPreferredHeightInRows = true;
    } else if (node.name == "spacer") {
        widget.kind = WidgetKind::Spacer;
        widget.preferredHeight =
            fontHeights_.footer + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        widget.fixedPreferredHeightInRows = true;
    } else if (node.name == "vertical_spring") {
        widget.kind = WidgetKind::VerticalSpring;
    } else if (node.name == "drive_usage_list") {
        widget.kind = WidgetKind::DriveUsageList;
        const int count = static_cast<int>(config_.storage.drives.size());
        widget.preferredHeight = (count > 0 ? EffectiveDriveHeaderHeight() : 0) + (count * EffectiveDriveRowHeight());
    } else if (node.name == "clock_time") {
        widget.kind = WidgetKind::ClockTime;
        widget.preferredHeight = fontHeights_.clockTime;
        widget.fixedPreferredHeightInRows = true;
    } else if (node.name == "clock_date") {
        widget.kind = WidgetKind::ClockDate;
        widget.preferredHeight = fontHeights_.clockDate;
        widget.fixedPreferredHeightInRows = true;
    }
    return widget;
}

bool DashboardRenderer::UsesFixedPreferredHeightInRows(const ResolvedWidgetLayout& widget) const {
    return widget.fixedPreferredHeightInRows;
}

const LayoutCardConfig* DashboardRenderer::FindCardConfigById(const std::string& id) const {
    const auto it = std::find_if(
        config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) { return card.id == id; });
    return it != config_.layout.cards.end() ? &(*it) : nullptr;
}
