#include "dashboard_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
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

UINT GetPanelIconResourceId(const std::string& iconName) {
    if (iconName == "cpu") return IDR_PANEL_ICON_CPU;
    if (iconName == "gpu") return IDR_PANEL_ICON_GPU;
    if (iconName == "network") return IDR_PANEL_ICON_NETWORK;
    if (iconName == "storage") return IDR_PANEL_ICON_STORAGE;
    if (iconName == "time") return IDR_PANEL_ICON_TIME;
    return 0;
}

bool IsContainerNode(const LayoutNodeConfig& node) {
    return node.name == "columns" || node.name == "stack" || node.name == "stack_top" || node.name == "center";
}

bool IsDashboardContainerNode(const LayoutNodeConfig& node) {
    return node.name == "rows" || node.name == "columns";
}

bool ContainsCardReference(const std::vector<std::string>& stack, const std::string& cardId) {
    return std::find(stack.begin(), stack.end(), cardId) != stack.end();
}

HFONT CreateUiFont(const UiFontConfig& font) {
    const std::wstring face = WideFromUtf8(font.face);
    return CreateFontW(-font.size, 0, 0, 0, font.weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, face.c_str());
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
    return POINT{
        cx + static_cast<LONG>(std::round(std::cos(radians) * radius)),
        cy - static_cast<LONG>(std::round(std::sin(radians) * radius))
    };
}

Gdiplus::PointF GaugePoint(float cx, float cy, float radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return Gdiplus::PointF(
        cx + static_cast<Gdiplus::REAL>(std::cos(radians) * radius),
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

std::string FormatRect(const RECT& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
        std::to_string(rect.right) + "," + std::to_string(rect.bottom) + ")";
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
    config_ = config;
}

void DashboardRenderer::SetRenderScale(double scale) {
    renderScale_ = std::clamp(scale, 0.1, 16.0);
}

void DashboardRenderer::SetRenderMode(RenderMode mode) {
    renderMode_ = mode;
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

bool DashboardRenderer::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    lastError_.clear();
    if (!InitializeGdiplus() || !LoadPanelIcons()) {
        return false;
    }
    if (fonts_.title == nullptr) {
        UiFontConfig titleFont = config_.layout.fonts.title;
        UiFontConfig bigFont = config_.layout.fonts.big;
        UiFontConfig valueFont = config_.layout.fonts.value;
        UiFontConfig labelFont = config_.layout.fonts.label;
        UiFontConfig smallFont = config_.layout.fonts.smallText;
        titleFont.size = ScaleLogical(titleFont.size);
        bigFont.size = ScaleLogical(bigFont.size);
        valueFont.size = ScaleLogical(valueFont.size);
        labelFont.size = ScaleLogical(labelFont.size);
        smallFont.size = ScaleLogical(smallFont.size);
        fonts_.title = CreateUiFont(titleFont);
        fonts_.big = CreateUiFont(bigFont);
        fonts_.value = CreateUiFont(valueFont);
        fonts_.label = CreateUiFont(labelFont);
        fonts_.smallFont = CreateUiFont(smallFont);
    }
    if (fonts_.title == nullptr || fonts_.big == nullptr || fonts_.value == nullptr ||
        fonts_.label == nullptr || fonts_.smallFont == nullptr) {
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
    DeleteObject(fonts_.title);
    DeleteObject(fonts_.big);
    DeleteObject(fonts_.value);
    DeleteObject(fonts_.label);
    DeleteObject(fonts_.smallFont);
    fonts_ = {};
    fontHeights_ = {};
    measuredWidths_ = {};
    resolvedLayout_ = {};
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
    fontHeights_.smallText = measure(fonts_.smallFont);
    measuredWidths_.throughputLabel = std::max(
        MeasureTextSize(hdc, fonts_.smallFont, "Read").cx,
        MeasureTextSize(hdc, fonts_.smallFont, "Write").cx) + std::max(0, ScaleLogical(config_.layout.throughput.labelPadding));
    measuredWidths_.throughputAxis = MeasureTextSize(hdc, fonts_.smallFont, "1000").cx +
        std::max(0, ScaleLogical(config_.layout.throughput.axisPadding));
    measuredWidths_.driveLabel = MeasureTextSize(hdc, fonts_.label, "W:").cx;
    measuredWidths_.drivePercent = MeasureTextSize(hdc, fonts_.label, "100%").cx +
        std::max(0, ScaleLogical(config_.layout.driveUsageList.percentPadding));
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    WriteTrace("renderer:font_metrics title=" + std::to_string(fontHeights_.title) +
        " big=" + std::to_string(fontHeights_.big) +
        " value=" + std::to_string(fontHeights_.value) +
        " label=" + std::to_string(fontHeights_.label) +
        " small=" + std::to_string(fontHeights_.smallText) +
        " render_scale=" + std::to_string(renderScale_) +
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
        " title_or_icon=" + std::to_string(titleHeight) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveMetricRowHeight() const {
    const int textHeight = std::max(fontHeights_.label, fontHeights_.value);
    const int barHeight = std::max(1, ScaleLogical(config_.layout.metricList.barHeight));
    const int verticalGap = std::max(0, ScaleLogical(config_.layout.metricList.verticalGap));
    const int computed = textHeight + verticalGap + barHeight;
    WriteTrace("renderer:layout_metric_row_height text=" + std::to_string(textHeight) +
        " bar=" + std::to_string(barHeight) +
        " gap=" + std::to_string(verticalGap) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveDriveHeaderHeight() const {
    const int verticalGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.verticalGap));
    const int computed = fontHeights_.smallText + verticalGap;
    WriteTrace("renderer:layout_drive_header_height text=" + std::to_string(fontHeights_.smallText) +
        " gap=" + std::to_string(verticalGap) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveDriveRowHeight() const {
    const int textHeight = std::max(fontHeights_.label, fontHeights_.smallText);
    const int barHeight = std::max(1, ScaleLogical(config_.layout.driveUsageList.barHeight));
    const int verticalGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.verticalGap));
    const int computed = std::max(textHeight, barHeight) + verticalGap;
    WriteTrace("renderer:layout_drive_row_height text=" + std::to_string(textHeight) +
        " bar=" + std::to_string(barHeight) +
        " gap=" + std::to_string(verticalGap) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::PreferredNodeHeight(const LayoutNodeConfig& node, int) const {
    if (node.name == "stack_top") {
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
    if (node.name == "text") {
        const int height = fontHeights_.label + std::max(0, ScaleLogical(config_.layout.text.preferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "network_footer") {
        const int height = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "spacer") {
        const int height = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
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
        const std::string param = node.parameter;
        const int count = static_cast<int>(Split(param, ',').size());
        const int height = (count > 0 ? EffectiveDriveHeaderHeight() : 0) + (count * EffectiveDriveRowHeight());
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" rows=" + std::to_string(count) +
            " value=" + std::to_string(height));
        return height;
    }
    if (node.name == "throughput") {
        const int height = fontHeights_.smallText + ScaleLogical(config_.layout.throughput.headerGap) +
            std::max(1, ScaleLogical(config_.layout.throughput.graphHeight));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "clock_time") {
        const int height = fontHeights_.big + std::max(0, ScaleLogical(config_.layout.clockTime.padding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (node.name == "clock_date") {
        const int height = fontHeights_.value + std::max(0, ScaleLogical(config_.layout.clockDate.padding));
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

DashboardRenderer::ResolvedWidgetLayout DashboardRenderer::ResolveWidgetLayout(const LayoutNodeConfig& node, const RECT& rect) const {
    ResolvedWidgetLayout widget;
    widget.rect = rect;
    if (node.name == "text") {
        widget.kind = WidgetKind::Text;
        widget.binding.metric = node.parameter;
        widget.preferredHeight = fontHeights_.label + std::max(0, ScaleLogical(config_.layout.text.preferredPadding));
        widget.fixedPreferredHeightInStack = true;
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
        widget.preferredHeight = fontHeights_.smallText + ScaleLogical(config_.layout.throughput.headerGap) +
            std::max(1, ScaleLogical(config_.layout.throughput.graphHeight));
    } else if (node.name == "network_footer") {
        widget.kind = WidgetKind::NetworkFooter;
        widget.preferredHeight = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        widget.fixedPreferredHeightInStack = true;
    } else if (node.name == "spacer") {
        widget.kind = WidgetKind::Spacer;
        widget.preferredHeight = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.networkFooter.preferredPadding));
        widget.fixedPreferredHeightInStack = true;
    } else if (node.name == "drive_usage_list") {
        widget.kind = WidgetKind::DriveUsageList;
        widget.binding.param = node.parameter;
        const int count = static_cast<int>(Split(node.parameter, ',').size());
        widget.preferredHeight = (count > 0 ? EffectiveDriveHeaderHeight() : 0) + (count * EffectiveDriveRowHeight());
    } else if (node.name == "clock_time") {
        widget.kind = WidgetKind::ClockTime;
        widget.preferredHeight = fontHeights_.big + std::max(0, ScaleLogical(config_.layout.clockTime.padding));
    } else if (node.name == "clock_date") {
        widget.kind = WidgetKind::ClockDate;
        widget.preferredHeight = fontHeights_.value + std::max(0, ScaleLogical(config_.layout.clockDate.padding));
    }
    return widget;
}

bool DashboardRenderer::UsesFixedPreferredHeightInStack(const ResolvedWidgetLayout& widget) const {
    return widget.fixedPreferredHeightInStack;
}

const LayoutCardConfig* DashboardRenderer::FindCardConfigById(const std::string& id) const {
    const auto it = std::find_if(config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) {
        return card.id == id;
    });
    return it != config_.layout.cards.end() ? &(*it) : nullptr;
}

void DashboardRenderer::ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets) {
    std::vector<std::string> cardReferenceStack;
    ResolveNodeWidgetsInternal(node, rect, widgets, cardReferenceStack);
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node, const RECT& rect,
    std::vector<ResolvedWidgetLayout>& widgets, std::vector<std::string>& cardReferenceStack) {
    WriteTrace("renderer:layout_resolve_node name=\"" + node.name + "\" weight=" + std::to_string(node.weight) +
        " " + FormatRect(rect) + " children=" + std::to_string(node.children.size()));
    if (node.cardReference) {
        if (ContainsCardReference(cardReferenceStack, node.name)) {
            WriteTrace("renderer:layout_card_ref_cycle id=\"" + node.name + "\"");
            return;
        }
        const LayoutCardConfig* referencedCard = FindCardConfigById(node.name);
        if (referencedCard == nullptr) {
            WriteTrace("renderer:layout_card_ref_missing id=\"" + node.name + "\"");
            return;
        }
        WriteTrace("renderer:layout_card_ref id=\"" + node.name + "\" " + FormatRect(rect));
        cardReferenceStack.push_back(node.name);
        ResolveNodeWidgetsInternal(referencedCard->layout, rect, widgets, cardReferenceStack);
        cardReferenceStack.pop_back();
        return;
    }
    if (!IsContainerNode(node)) {
        ResolvedWidgetLayout widget = ResolveWidgetLayout(node, rect);
        WriteTrace("renderer:layout_widget_resolved kind=\"" + node.name + "\" " + FormatRect(widget.rect) +
            (widget.binding.metric.empty() ? "" : " metric=\"" + widget.binding.metric + "\"") +
            (widget.binding.param.empty() ? "" : " param=\"" + widget.binding.param + "\""));
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = node.name == "columns";
    const bool topPacked = node.name == "stack_top";
    const int gap = horizontal ? ScaleLogical(config_.layout.cardStyle.columnGap) : ScaleLogical(config_.layout.cardStyle.widgetLineGap);
    if (topPacked) {
        int cursor = static_cast<int>(rect.top);
        for (size_t i = 0; i < node.children.size(); ++i) {
            const auto& child = node.children[i];
            int preferred = PreferredNodeHeight(child, static_cast<int>(rect.right - rect.left));
            if (preferred <= 0) {
                preferred = std::max(0, (static_cast<int>(rect.bottom) - cursor) / static_cast<int>(node.children.size() - i));
            }
            if (cursor >= static_cast<int>(rect.bottom)) {
                break;
            }
            RECT childRect{rect.left, cursor, rect.right, std::min(static_cast<int>(rect.bottom), cursor + preferred)};
            WriteTrace("renderer:layout_top_packed_child parent=\"" + node.name + "\" child=\"" + child.name +
                "\" preferred=" + std::to_string(preferred) + " gap=" + std::to_string(gap) +
                " " + FormatRect(childRect));
            ResolveNodeWidgetsInternal(child, childRect, widgets, cardReferenceStack);
            cursor = static_cast<int>(childRect.bottom) + gap;
        }
        return;
    }

    const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
        gap * static_cast<int>(std::max<size_t>(0, node.children.size() - 1));
    int reservedPreferred = 0;
    int totalWeight = 0;
    if (!horizontal) {
        for (const auto& child : node.children) {
            const ResolvedWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
            if (UsesFixedPreferredHeightInStack(resolvedChild)) {
                reservedPreferred += std::max(0, resolvedChild.preferredHeight);
            } else {
                totalWeight += std::max(1, child.weight);
            }
        }
    } else {
        for (const auto& child : node.children) {
            totalWeight += std::max(1, child.weight);
        }
    }
    const int distributableAvailable = horizontal ? totalAvailable : std::max(0, totalAvailable - reservedPreferred);
    if (horizontal && totalWeight <= 0) {
        return;
    }

    int remainingAvailable = totalAvailable;
    int remainingDistributable = distributableAvailable;
    int cursor = horizontal ? rect.left : rect.top;
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        const ResolvedWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
        const bool fixedPreferred = !horizontal && UsesFixedPreferredHeightInStack(resolvedChild);
        const int childWeight = fixedPreferred ? 0 : std::max(1, child.weight);
        const int remainingWeight = std::max(1, totalWeight);
        int size = 0;
        if (fixedPreferred) {
            size = std::max(0, resolvedChild.preferredHeight);
        } else if (i + 1 == node.children.size()) {
            size = (horizontal ? rect.right : rect.bottom) - cursor;
        } else {
            size = std::max(0, remainingDistributable * childWeight / remainingWeight);
        }
        const int remainingExtent = std::max(0, static_cast<int>((horizontal ? rect.right : rect.bottom) - cursor));
        size = std::min(size, remainingExtent);

        RECT childRect = rect;
        if (horizontal) {
            childRect.left = cursor;
            childRect.right = cursor + size;
        } else {
            childRect.top = cursor;
            childRect.bottom = cursor + size;
        }

        WriteTrace("renderer:layout_weighted_child parent=\"" + node.name + "\" child=\"" + child.name +
            "\" weight=" + std::to_string(childWeight) +
            " gap=" + std::to_string(gap) +
            " size=" + std::to_string(size) +
            " " + FormatRect(childRect));
        ResolveNodeWidgetsInternal(child, childRect, widgets, cardReferenceStack);
        cursor += size + gap;
        remainingAvailable -= size;
        if (!fixedPreferred) {
            remainingDistributable -= size;
            totalWeight -= childWeight;
        }
    }
}

bool DashboardRenderer::ResolveLayout() {
    resolvedLayout_ = {};
    resolvedLayout_.windowWidth = WindowWidth();
    resolvedLayout_.windowHeight = WindowHeight();

    const RECT dashboardRect{
        ScaleLogical(config_.layout.dashboard.outerMargin),
        ScaleLogical(config_.layout.dashboard.outerMargin),
        WindowWidth() - ScaleLogical(config_.layout.dashboard.outerMargin),
        WindowHeight() - ScaleLogical(config_.layout.dashboard.outerMargin)
    };

    if (config_.layout.structure.cardsLayout.name.empty()) {
        lastError_ = "renderer:layout_missing_cards_root";
        return false;
    }

    WriteTrace("renderer:layout_begin window=" + std::to_string(resolvedLayout_.windowWidth) + "x" +
        std::to_string(resolvedLayout_.windowHeight) + " " + FormatRect(dashboardRect) +
        " cards_root=\"" + config_.layout.structure.cardsLayout.name + "\"");

    const auto resolveCard = [&](const LayoutNodeConfig& node, const RECT& rect) {
        const auto cardIt = std::find_if(config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) {
            return card.id == node.name;
        });
        if (cardIt == config_.layout.cards.end()) {
            return;
        }

        ResolvedCardLayout card;
        card.id = cardIt->id;
        card.title = cardIt->title;
        card.iconName = cardIt->icon;
        card.hasHeader = !card.title.empty() || !card.iconName.empty();
        card.rect = rect;

        const int padding = ScaleLogical(config_.layout.cardStyle.cardPadding);
        const int iconSize = ScaleLogical(config_.layout.cardStyle.headerIconSize);
        const int headerHeight = card.hasHeader ? EffectiveHeaderHeight() : 0;
        if (!card.iconName.empty()) {
            card.iconRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + std::max(0, (headerHeight - iconSize) / 2),
                card.rect.left + padding + iconSize,
                card.rect.top + padding + std::max(0, (headerHeight - iconSize) / 2) + iconSize
            };
        } else {
            card.iconRect = RECT{card.rect.left + padding, card.rect.top + padding, card.rect.left + padding, card.rect.top + padding};
        }
        const int titleLeft = !card.iconName.empty()
            ? card.iconRect.right + ScaleLogical(config_.layout.cardStyle.headerGap)
            : card.rect.left + padding;
        card.titleRect = RECT{
            titleLeft,
            card.rect.top + padding,
            card.rect.right - padding,
            card.rect.top + padding + headerHeight
        };
        card.contentRect = RECT{
            card.rect.left + padding,
            card.rect.top + padding + headerHeight + ScaleLogical(config_.layout.cardStyle.contentGap),
            card.rect.right - padding,
            card.rect.bottom - padding
        };

        WriteTrace("renderer:layout_card id=\"" + card.id + "\" " + FormatRect(card.rect) +
            " title=" + FormatRect(card.titleRect) +
            " icon=" + FormatRect(card.iconRect) +
            " content=" + FormatRect(card.contentRect));
        ResolveNodeWidgets(cardIt->layout, card.contentRect, card.widgets);
        resolvedLayout_.cards.push_back(std::move(card));
    };

    std::function<void(const LayoutNodeConfig&, const RECT&)> resolveDashboardNode =
        [&](const LayoutNodeConfig& node, const RECT& rect) {
            if (!IsDashboardContainerNode(node)) {
                resolveCard(node, rect);
                return;
            }

            const bool horizontal = node.name == "columns";
            const int gap = horizontal ? ScaleLogical(config_.layout.dashboard.cardGap) : ScaleLogical(config_.layout.dashboard.rowGap);
            int totalWeight = 0;
            for (const auto& child : node.children) {
                totalWeight += std::max(1, child.weight);
            }
            if (totalWeight <= 0) {
                return;
            }

            const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                gap * static_cast<int>(std::max<size_t>(0, node.children.size() - 1));
            int remainingAvailable = totalAvailable;
            int cursor = horizontal ? rect.left : rect.top;
            int remainingWeight = totalWeight;
            for (size_t i = 0; i < node.children.size(); ++i) {
                const auto& child = node.children[i];
                const int childWeight = std::max(1, child.weight);
                const int size = (i + 1 == node.children.size())
                    ? ((horizontal ? rect.right : rect.bottom) - cursor)
                    : std::max(0, remainingAvailable * childWeight / std::max(1, remainingWeight));

                RECT childRect = rect;
                if (horizontal) {
                    childRect.left = cursor;
                    childRect.right = cursor + size;
                } else {
                    childRect.top = cursor;
                    childRect.bottom = cursor + size;
                }

                WriteTrace("renderer:layout_dashboard_child parent=\"" + node.name + "\" child=\"" + child.name +
                    "\" weight=" + std::to_string(childWeight) +
                    " gap=" + std::to_string(gap) +
                    " size=" + std::to_string(size) +
                    " " + FormatRect(childRect));
                resolveDashboardNode(child, childRect);
                cursor += size + gap;
                remainingAvailable -= size;
                remainingWeight -= childWeight;
            }
        };

    resolveDashboardNode(config_.layout.structure.cardsLayout, dashboardRect);

    if (resolvedLayout_.cards.empty()) {
        lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" + config_.layout.structure.cardsLayout.name + "\"";
        return false;
    }
    WriteTrace("renderer:layout_done cards=" + std::to_string(resolvedLayout_.cards.size()));
    return true;
}

void DashboardRenderer::DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    const std::wstring wideText = WideFromUtf8(text);
    DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
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
        DrawTextBlock(hdc, card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

void DashboardRenderer::DrawGauge(HDC hdc, int cx, int cy, int radius, const DashboardGaugeMetric& metric, const std::string& label) {
    const float segmentThickness = static_cast<float>(std::max(1, ScaleLogical(config_.layout.gauge.ringThickness)));
    const int segmentCount = std::max(1, config_.layout.gauge.segmentCount);
    const double totalSweep = std::max(0.0, config_.layout.gauge.sweepDegrees);
    const double gapSweep = std::max(0.0, 360.0 - totalSweep);
    const double slotSweep = totalSweep / static_cast<double>(segmentCount);
    const double segmentSweep = std::clamp(slotSweep - config_.layout.gauge.segmentGapDegrees, 0.0, slotSweep);
    const double clampedPercent = std::clamp(metric.percent, 0.0, 100.0);
    const int filledSegments = clampedPercent <= 0.0 ? 0
        : std::clamp(static_cast<int>(std::ceil(clampedPercent * static_cast<double>(segmentCount) / 100.0)),
            1, segmentCount);
    const double clampedPeakRatio = std::clamp(metric.peakRatio, 0.0, 1.0);
    const int peakSegment = clampedPeakRatio <= 0.0 ? -1
        : std::clamp(static_cast<int>(std::ceil(clampedPeakRatio * static_cast<double>(segmentCount))) - 1,
            0, segmentCount - 1);
    const double gaugeStart = 90.0 + gapSweep / 2.0;
    const float segmentRadius = static_cast<float>(radius);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const Gdiplus::Color trackColor(255, GetRValue(ToColorRef(config_.layout.colors.trackColor)),
        GetGValue(ToColorRef(config_.layout.colors.trackColor)), GetBValue(ToColorRef(config_.layout.colors.trackColor)));
    const Gdiplus::Color usageColor(255, GetRValue(AccentColor()), GetGValue(AccentColor()), GetBValue(AccentColor()));
    const Gdiplus::Color ghostColor(96, GetRValue(AccentColor()), GetGValue(AccentColor()), GetBValue(AccentColor()));

    for (int i = 0; i < segmentCount; ++i) {
        const double slotStart = gaugeStart + slotSweep * static_cast<double>(i);
        FillGaugeSegment(graphics, static_cast<float>(cx), static_cast<float>(cy), segmentRadius,
            segmentThickness, slotStart, segmentSweep, trackColor);

        if (renderMode_ != RenderMode::Blank && i < filledSegments) {
            FillGaugeSegment(graphics, static_cast<float>(cx), static_cast<float>(cy), segmentRadius,
                segmentThickness, slotStart, segmentSweep, usageColor);
        }

        if (renderMode_ != RenderMode::Blank && i == peakSegment) {
            FillGaugeSegment(graphics, static_cast<float>(cx), static_cast<float>(cy), segmentRadius,
                segmentThickness, slotStart, segmentSweep, ghostColor);
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
        DrawTextBlock(hdc, numberRect, number, fonts_.big, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    RECT labelRect{cx - halfWidth,
        cy + ScaleLogical(config_.layout.gauge.labelTop),
        cx + halfWidth,
        cy + ScaleLogical(config_.layout.gauge.labelBottom)};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
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

void DashboardRenderer::DrawMetricRow(HDC hdc, const RECT& rect, const DashboardMetricRow& row) {
    const int rowHeight = EffectiveMetricRowHeight();
    const int labelWidth = std::max(1, ScaleLogical(config_.layout.metricList.labelWidth));
    const int valueGap = std::max(0, ScaleLogical(config_.layout.metricList.valueGap));
    RECT labelRect{rect.left, rect.top, std::min(rect.right, rect.left + labelWidth), rect.bottom};
    RECT valueRect{std::min(rect.right, labelRect.right + valueGap), rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, row.label, fonts_.label, MutedTextColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (renderMode_ != RenderMode::Blank) {
        DrawTextBlock(hdc, valueRect, row.valueText, fonts_.value, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    const int metricBarHeight = std::max(1, ScaleLogical(config_.layout.metricList.barHeight));
    const int barBottom = std::min(static_cast<int>(rect.bottom), static_cast<int>(rect.top) + rowHeight);
    const int barTop = std::max(static_cast<int>(rect.top), barBottom - metricBarHeight);
    RECT barRect{valueRect.left, barTop, rect.right, barBottom};
    DrawPillBar(hdc, barRect, row.ratio, row.peakRatio, renderMode_ != RenderMode::Blank);
}

void DashboardRenderer::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue,
    double guideStepMbps, double timeMarkerOffsetSamples, double timeMarkerIntervalSamples) {
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
        DrawTextBlock(hdc, maxRect, maxLabel, fonts_.smallFont, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
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

void DashboardRenderer::DrawThroughputWidget(HDC hdc, const RECT& rect, const DashboardThroughputMetric& metric) {
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
    DrawTextBlock(hdc, labelRect, metric.label, fonts_.smallFont, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (renderMode_ != RenderMode::Blank) {
        DrawTextBlock(hdc, numberRect, buffer, fonts_.smallFont, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    DrawGraph(hdc, graphRect, metric.history, metric.maxGraph, metric.guideStepMbps,
        metric.timeMarkerOffsetSamples, metric.timeMarkerIntervalSamples);
}

void DashboardRenderer::DrawDriveUsageWidget(HDC hdc, const RECT& rect, const std::vector<DashboardDriveRow>& rows) {
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
    RECT usageHeaderRect{headerBarRect.left, header.top, headerPctRect.right, header.bottom};
    RECT headerReadLabelRect{headerReadRect.left - valueGap, headerReadRect.top, headerReadRect.right + valueGap, headerReadRect.bottom};
    RECT headerWriteLabelRect{headerWriteRect.left - valueGap, headerWriteRect.top, headerWriteRect.right + valueGap, headerWriteRect.bottom};
    DrawTextBlock(hdc, headerReadLabelRect, "R", fonts_.smallFont, MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
    DrawTextBlock(hdc, headerWriteLabelRect, "W", fonts_.smallFont, MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
    DrawTextBlock(hdc, usageHeaderRect, "Usage", fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, headerFreeRect, "Free", fonts_.smallFont, MutedTextColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    for (const auto& drive : rows) {
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

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawSegmentIndicator(hdc, readIndicatorRect, activitySegments, activitySegmentGap,
            renderMode_ == RenderMode::Blank ? 0.0 : drive.readActivity,
            ToColorRef(config_.layout.colors.trackColor), AccentColor());
        DrawSegmentIndicator(hdc, writeIndicatorRect, activitySegments, activitySegmentGap,
            renderMode_ == RenderMode::Blank ? 0.0 : drive.writeActivity,
            ToColorRef(config_.layout.colors.trackColor), AccentColor());
        DrawPillBar(hdc, barRect, drive.usedPercent / 100.0, std::nullopt, renderMode_ != RenderMode::Blank);

        if (renderMode_ != RenderMode::Blank) {
            char percent[16];
            sprintf_s(percent, "%.0f%%", drive.usedPercent);
            DrawTextBlock(hdc, pctRect, percent, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            DrawTextBlock(hdc, freeRect, drive.freeText, fonts_.smallFont, MutedTextColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
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
        DrawTextBlock(hdc, widget.rect, metrics.ResolveText(widget.binding.metric), fonts_.label, ForegroundColor(),
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    case WidgetKind::Gauge: {
        const DashboardGaugeMetric gaugeMetric = metrics.ResolveGauge(widget.binding.metric);
        const int width = widget.rect.right - widget.rect.left;
        const int height = widget.rect.bottom - widget.rect.top;
        const int radius = std::max(
            std::max(1, ScaleLogical(config_.layout.gauge.minRadius)),
            std::max(1, std::min(width, height) / 2 - std::max(0, ScaleLogical(config_.layout.gauge.outerPadding))));
        DrawGauge(hdc, widget.rect.left + width / 2, widget.rect.top + height / 2, radius, gaugeMetric, "Load");
        return;
    }
    case WidgetKind::MetricList: {
        const int rowHeight = EffectiveMetricRowHeight();
        const int savedDc = SaveDC(hdc);
        IntersectClipRect(hdc, widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.bottom);
        RECT rowRect{widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.top + rowHeight};
        for (const auto& row : metrics.ResolveMetricList(ParseMetricListEntries(widget.binding.param))) {
            DrawMetricRow(hdc, rowRect, row);
            OffsetRect(&rowRect, 0, rowHeight);
            if (rowRect.top >= widget.rect.bottom) {
                break;
            }
        }
        RestoreDC(hdc, savedDc);
        return;
    }
    case WidgetKind::Throughput:
        DrawThroughputWidget(hdc, widget.rect, metrics.ResolveThroughput(widget.binding.metric));
        return;
    case WidgetKind::NetworkFooter:
        if (renderMode_ != RenderMode::Blank) {
            DrawTextBlock(hdc, widget.rect, metrics.ResolveNetworkFooter(), fonts_.smallFont, ForegroundColor(),
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        return;
    case WidgetKind::Spacer:
        return;
    case WidgetKind::DriveUsageList:
        DrawDriveUsageWidget(hdc, widget.rect, metrics.ResolveDriveRows(Split(widget.binding.param, ',')));
        return;
    case WidgetKind::ClockTime:
        if (renderMode_ != RenderMode::Blank) {
            DrawTextBlock(hdc, widget.rect, metrics.ResolveClockTime(), fonts_.big, ForegroundColor(),
                DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        return;
    case WidgetKind::ClockDate:
        if (renderMode_ != RenderMode::Blank) {
            DrawTextBlock(hdc, widget.rect, metrics.ResolveClockDate(), fonts_.value, MutedTextColor(),
                DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        return;
    default:
        return;
    }
}

void DashboardRenderer::Draw(HDC hdc, const SystemSnapshot& snapshot) {
    DashboardMetricSource metrics(snapshot, config_.metricScales);
    for (const auto& card : resolvedLayout_.cards) {
        DrawPanel(hdc, card);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidget(hdc, widget, metrics);
        }
    }
}

bool DashboardRenderer::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
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
    Draw(memDc, snapshot);

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
