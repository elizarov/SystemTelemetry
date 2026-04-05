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

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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
    const std::string lowered = ToLower(iconName);
    if (lowered == "cpu") return IDR_PANEL_ICON_CPU;
    if (lowered == "gpu") return IDR_PANEL_ICON_GPU;
    if (lowered == "network") return IDR_PANEL_ICON_NETWORK;
    if (lowered == "storage") return IDR_PANEL_ICON_STORAGE;
    if (lowered == "time") return IDR_PANEL_ICON_TIME;
    return 0;
}

bool IsContainerNode(const LayoutNodeConfig& node) {
    const std::string lowered = ToLower(node.name);
    return lowered == "columns" || lowered == "stack" || lowered == "stack_top" || lowered == "center";
}

bool IsDashboardContainerNode(const LayoutNodeConfig& node) {
    const std::string lowered = ToLower(node.name);
    return lowered == "rows" || lowered == "columns";
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

double DashboardRenderer::RenderScale() const {
    return renderScale_;
}

const std::string& DashboardRenderer::LastError() const {
    return lastError_;
}

int DashboardRenderer::WindowWidth() const {
    return std::max(1, ScaleLogical(config_.layout.windowWidth));
}

int DashboardRenderer::WindowHeight() const {
    return std::max(1, ScaleLogical(config_.layout.windowHeight));
}

COLORREF DashboardRenderer::BackgroundColor() const {
    return ToColorRef(config_.layout.backgroundColor);
}

COLORREF DashboardRenderer::ForegroundColor() const {
    return ToColorRef(config_.layout.foregroundColor);
}

COLORREF DashboardRenderer::AccentColor() const {
    return ToColorRef(config_.layout.accentColor);
}

COLORREF DashboardRenderer::MutedTextColor() const {
    return ToColorRef(config_.layout.mutedTextColor);
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
        UiFontConfig titleFont = config_.layout.titleFont;
        UiFontConfig bigFont = config_.layout.bigFont;
        UiFontConfig valueFont = config_.layout.valueFont;
        UiFontConfig labelFont = config_.layout.labelFont;
        UiFontConfig smallFont = config_.layout.smallFont;
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
            uniqueIcons.insert(ToLower(card.icon));
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
        MeasureTextSize(hdc, fonts_.smallFont, "Write").cx) + std::max(0, ScaleLogical(config_.layout.throughputLabelPadding));
    measuredWidths_.throughputAxis = MeasureTextSize(hdc, fonts_.smallFont, "1000").cx +
        std::max(0, ScaleLogical(config_.layout.throughputAxisPadding));
    measuredWidths_.driveLabel = MeasureTextSize(hdc, fonts_.label, "W:").cx +
        std::max(0, ScaleLogical(config_.layout.driveLabelPadding));
    measuredWidths_.drivePercent = MeasureTextSize(hdc, fonts_.label, "100%").cx +
        std::max(0, ScaleLogical(config_.layout.drivePercentPadding));
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
    const int titleHeight = std::max(fontHeights_.title, ScaleLogical(config_.layout.headerIconSize));
    const int configured = ScaleLogical(config_.layout.headerHeight);
    const int computed = std::max(configured, titleHeight);
    WriteTrace("renderer:layout_header_height configured=" + std::to_string(configured) +
        " title_or_icon=" + std::to_string(titleHeight) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveMetricRowHeight() const {
    const int textHeight = std::max(fontHeights_.label, fontHeights_.value);
    const int barHeight = std::max(1, ScaleLogical(config_.layout.metricBarHeight));
    const int verticalGap = std::max(0, ScaleLogical(config_.layout.metricVerticalGap));
    const int computed = textHeight + verticalGap + barHeight;
    WriteTrace("renderer:layout_metric_row_height text=" + std::to_string(textHeight) +
        " bar=" + std::to_string(barHeight) +
        " gap=" + std::to_string(verticalGap) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::EffectiveDriveRowHeight() const {
    const int textHeight = std::max(fontHeights_.label, fontHeights_.smallText);
    const int barHeight = std::max(1, ScaleLogical(config_.layout.driveBarHeight));
    const int verticalGap = std::max(0, ScaleLogical(config_.layout.driveVerticalGap));
    const int computed = std::max(textHeight, barHeight) + verticalGap;
    WriteTrace("renderer:layout_drive_row_height text=" + std::to_string(textHeight) +
        " bar=" + std::to_string(barHeight) +
        " gap=" + std::to_string(verticalGap) +
        " effective=" + std::to_string(computed));
    return computed;
}

int DashboardRenderer::PreferredNodeHeight(const LayoutNodeConfig& node, int) const {
    const std::string lowered = ToLower(node.name);
    if (lowered == "stack_top") {
        int total = 0;
        for (size_t i = 0; i < node.children.size(); ++i) {
            total += PreferredNodeHeight(node.children[i], 0);
            if (i + 1 < node.children.size()) {
                total += ScaleLogical(config_.layout.widgetLineGap);
            }
        }
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(total));
        return total;
    }
    if (lowered == "text") {
        const int height = fontHeights_.label + std::max(0, ScaleLogical(config_.layout.textPreferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (lowered == "network_footer") {
        const int height = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.footerPreferredPadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (lowered == "metric_list") {
        const std::string param = node.parameter;
        const int count = static_cast<int>(Split(param, ',').size());
        const int height = count * EffectiveMetricRowHeight();
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" rows=" + std::to_string(count) +
            " value=" + std::to_string(height));
        return height;
    }
    if (lowered == "drive_usage_list") {
        const std::string param = node.parameter;
        const int count = static_cast<int>(Split(param, ',').size());
        const int height = count * EffectiveDriveRowHeight();
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" rows=" + std::to_string(count) +
            " value=" + std::to_string(height));
        return height;
    }
    if (lowered == "throughput") {
        const int height = fontHeights_.smallText + ScaleLogical(config_.layout.throughputHeaderGap) +
            std::max(1, ScaleLogical(config_.layout.throughputGraphHeight));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (lowered == "clock_time") {
        const int height = fontHeights_.big + std::max(0, ScaleLogical(config_.layout.clockTimePadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (lowered == "clock_date") {
        const int height = fontHeights_.value + std::max(0, ScaleLogical(config_.layout.clockDatePadding));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    if (lowered == "gauge") {
        const int height = std::max(1, ScaleLogical(config_.layout.gaugePreferredSize));
        WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(height));
        return height;
    }
    WriteTrace("renderer:layout_preferred_height node=\"" + node.name + "\" value=0");
    return 0;
}

void DashboardRenderer::ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets) {
    WriteTrace("renderer:layout_resolve_node name=\"" + node.name + "\" weight=" + std::to_string(node.weight) +
        " " + FormatRect(rect) + " children=" + std::to_string(node.children.size()));
    if (!IsContainerNode(node)) {
        ResolvedWidgetLayout widget;
        const std::string lowered = ToLower(node.name);
        if (lowered == "text") {
            widget.kind = WidgetKind::Text;
            widget.binding.metric = node.parameter;
        } else if (lowered == "gauge") {
            widget.kind = WidgetKind::Gauge;
            widget.binding.metric = node.parameter;
        } else if (lowered == "metric_list") {
            widget.kind = WidgetKind::MetricList;
            widget.binding.param = node.parameter;
        } else if (lowered == "throughput") {
            widget.kind = WidgetKind::Throughput;
            widget.binding.metric = node.parameter;
        } else if (lowered == "network_footer") {
            widget.kind = WidgetKind::NetworkFooter;
        } else if (lowered == "spacer") {
            widget.kind = WidgetKind::Spacer;
        } else if (lowered == "drive_usage_list") {
            widget.kind = WidgetKind::DriveUsageList;
            widget.binding.param = node.parameter;
        } else if (lowered == "clock_time") {
            widget.kind = WidgetKind::ClockTime;
        } else if (lowered == "clock_date") {
            widget.kind = WidgetKind::ClockDate;
        }
        widget.rect = rect;
        WriteTrace("renderer:layout_widget_resolved kind=\"" + node.name + "\" " + FormatRect(widget.rect) +
            (widget.binding.metric.empty() ? "" : " metric=\"" + widget.binding.metric + "\"") +
            (widget.binding.param.empty() ? "" : " param=\"" + widget.binding.param + "\""));
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = ToLower(node.name) == "columns";
    const bool topPacked = ToLower(node.name) == "stack_top";
    const int gap = horizontal ? ScaleLogical(config_.layout.columnGap) : ScaleLogical(config_.layout.widgetLineGap);
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
            ResolveNodeWidgets(child, childRect, widgets);
            cursor = static_cast<int>(childRect.bottom) + gap;
        }
        return;
    }

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
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        const int childWeight = std::max(1, child.weight);
        const int remainingWeight = std::max(1, totalWeight);
        const int size = (i + 1 == node.children.size())
            ? ((horizontal ? rect.right : rect.bottom) - cursor)
            : std::max(0, remainingAvailable * childWeight / remainingWeight);

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
        ResolveNodeWidgets(child, childRect, widgets);
        cursor += size + gap;
        remainingAvailable -= size;
        totalWeight -= childWeight;
    }
}

bool DashboardRenderer::ResolveLayout() {
    resolvedLayout_ = {};
    resolvedLayout_.windowWidth = WindowWidth();
    resolvedLayout_.windowHeight = WindowHeight();

    const RECT dashboardRect{
        ScaleLogical(config_.layout.outerMargin),
        ScaleLogical(config_.layout.outerMargin),
        WindowWidth() - ScaleLogical(config_.layout.outerMargin),
        WindowHeight() - ScaleLogical(config_.layout.outerMargin)
    };

    if (config_.layout.cardsLayout.name.empty()) {
        lastError_ = "renderer:layout_missing_cards_root";
        return false;
    }

    WriteTrace("renderer:layout_begin window=" + std::to_string(resolvedLayout_.windowWidth) + "x" +
        std::to_string(resolvedLayout_.windowHeight) + " " + FormatRect(dashboardRect) +
        " cards_root=\"" + config_.layout.cardsLayout.name + "\"");

    const auto resolveCard = [&](const LayoutNodeConfig& node, const RECT& rect) {
        const auto cardIt = std::find_if(config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) {
            return ToLower(card.id) == ToLower(node.name);
        });
        if (cardIt == config_.layout.cards.end()) {
            return;
        }

        ResolvedCardLayout card;
        card.id = cardIt->id;
        card.title = cardIt->title;
        card.iconName = cardIt->icon;
        card.rect = rect;

        const int padding = ScaleLogical(config_.layout.cardPadding);
        const int iconSize = ScaleLogical(config_.layout.headerIconSize);
        const int headerHeight = EffectiveHeaderHeight();
        card.iconRect = RECT{
            card.rect.left + padding,
            card.rect.top + padding + std::max(0, (headerHeight - iconSize) / 2),
            card.rect.left + padding + iconSize,
            card.rect.top + padding + std::max(0, (headerHeight - iconSize) / 2) + iconSize
        };
        card.titleRect = RECT{
            card.iconRect.right + ScaleLogical(config_.layout.headerGap),
            card.rect.top + padding,
            card.rect.right - padding,
            card.rect.top + padding + headerHeight
        };
        card.contentRect = RECT{
            card.rect.left + padding,
            card.rect.top + padding + headerHeight + ScaleLogical(config_.layout.contentGap),
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

            const bool horizontal = ToLower(node.name) == "columns";
            const int gap = horizontal ? ScaleLogical(config_.layout.cardGap) : ScaleLogical(config_.layout.rowGap);
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

    resolveDashboardNode(config_.layout.cardsLayout, dashboardRect);

    if (resolvedLayout_.cards.empty()) {
        lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" + config_.layout.cardsLayout.name + "\"";
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
        return ToLower(entry.first) == ToLower(iconName);
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
    HPEN border = CreatePen(PS_SOLID, std::max(1, ScaleLogical(config_.layout.cardBorderWidth)),
        ToColorRef(config_.layout.panelBorderColor));
    HBRUSH fill = CreateSolidBrush(ToColorRef(config_.layout.panelFillColor));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    const int radius = std::max(1, ScaleLogical(config_.layout.cardRadius));
    RoundRect(hdc, card.rect.left, card.rect.top, card.rect.right, card.rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(border);
    DrawPanelIcon(hdc, card.iconName, card.iconRect);
    DrawTextBlock(hdc, card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

void DashboardRenderer::DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label) {
    const int penWidth = std::max(1, ScaleLogical(config_.layout.gaugeStrokeWidth));
    HPEN trackPen = CreatePen(PS_SOLID, penWidth, ToColorRef(config_.layout.trackColor));
    HPEN usagePen = CreatePen(PS_SOLID, penWidth, ToColorRef(config_.layout.accentColor));
    HGDIOBJ oldPen = SelectObject(hdc, trackPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const RECT bounds{cx - radius, cy - radius, cx + radius, cy + radius};
    Ellipse(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);

    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    const double sweep = 360.0 * clampedPercent / 100.0;
    if (sweep > 0.0) {
        SelectObject(hdc, usagePen);
        SetArcDirection(hdc, AD_CLOCKWISE);
        const POINT startValue = PolarPoint(cx, cy, radius, 90.0);
        MoveToEx(hdc, startValue.x, startValue.y, nullptr);
        AngleArc(hdc, cx, cy, radius, 90.0f, static_cast<FLOAT>(-sweep));
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(trackPen);
    DeleteObject(usagePen);

    const int halfWidth = std::max(1, ScaleLogical(config_.layout.gaugeTextHalfWidth));
    RECT numberRect{cx - halfWidth,
        cy - ScaleLogical(config_.layout.gaugeValueTop),
        cx + halfWidth,
        cy + ScaleLogical(config_.layout.gaugeValueBottom)};
    char number[16];
    sprintf_s(number, "%.0f%%", percent);
    DrawTextBlock(hdc, numberRect, number, fonts_.big, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT labelRect{cx - halfWidth,
        cy + ScaleLogical(config_.layout.gaugeLabelTop),
        cx + halfWidth,
        cy + ScaleLogical(config_.layout.gaugeLabelBottom)};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DashboardRenderer::DrawPillBar(HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio) {
    FillCapsule(hdc, rect, ToColorRef(config_.layout.trackColor), 255);

    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0) {
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
    const int labelWidth = std::max(1, ScaleLogical(config_.layout.metricLabelWidth));
    const int valueGap = std::max(0, ScaleLogical(config_.layout.metricValueGap));
    RECT labelRect{rect.left, rect.top, std::min(rect.right, rect.left + labelWidth), rect.bottom};
    RECT valueRect{std::min(rect.right, labelRect.right + valueGap), rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, row.label, fonts_.label, MutedTextColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, valueRect, row.valueText, fonts_.value, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const int metricBarHeight = std::max(1, ScaleLogical(config_.layout.metricBarHeight));
    const int barBottom = std::min(static_cast<int>(rect.bottom), static_cast<int>(rect.top) + rowHeight);
    const int barTop = std::max(static_cast<int>(rect.top), barBottom - metricBarHeight);
    RECT barRect{valueRect.left, barTop, rect.right, barBottom};
    DrawPillBar(hdc, barRect, row.ratio, row.peakRatio);
}

void DashboardRenderer::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue,
    double guideStepMbps, double timeMarkerOffsetSamples, double timeMarkerIntervalSamples) {
    HBRUSH bg = CreateSolidBrush(ToColorRef(config_.layout.graphBackgroundColor));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    const int axisWidth = std::max(1, measuredWidths_.throughputAxis);
    const int labelBandHeight = std::max(
        fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.graphLabelPadding)),
        std::max(1, ScaleLogical(config_.layout.graphLabelMinHeight)));
    const int graphTop = std::min(rect.bottom - 1, rect.top + labelBandHeight);
    const int graphLeft = rect.left + axisWidth;
    const int width = std::max<int>(1, rect.right - graphLeft - 1);
    const int height = std::max<int>(1, rect.bottom - rect.top - 1);
    const int graphRight = graphLeft + width;
    const int graphBottom = rect.bottom - 1;

    const int strokeWidth = std::max(1, ScaleLogical(config_.layout.graphStrokeWidth));
    const double guideStep = guideStepMbps > 0.0 ? guideStepMbps : 5.0;
    HBRUSH markerBrush = CreateSolidBrush(ToColorRef(config_.layout.graphMarkerColor));
    for (double tick = guideStep; tick < maxValue; tick += guideStep) {
        const double ratio = tick / maxValue;
        const int y = graphBottom - static_cast<int>(std::round(ratio * height));
        RECT lineRect{graphLeft, y, graphRight, std::min(graphBottom + 1, y + strokeWidth)};
        FillRect(hdc, &lineRect, markerBrush);
    }

    const double markerInterval = timeMarkerIntervalSamples > 0.0 ? timeMarkerIntervalSamples : 20.0;
    for (double sampleOffset = timeMarkerOffsetSamples; sampleOffset <= static_cast<double>(history.size() - 1) + markerInterval;
         sampleOffset += markerInterval) {
        const double clampedOffset = std::clamp(sampleOffset, 0.0, static_cast<double>(history.size() - 1));
        const int x = graphRight - static_cast<int>(std::round(
            clampedOffset * width / std::max<size_t>(1, history.size() - 1)));
        RECT lineRect{x, rect.top, std::min(graphRight + 1, x + strokeWidth), rect.bottom};
        FillRect(hdc, &lineRect, markerBrush);
    }

    DeleteObject(markerBrush);

    HBRUSH axisBrush = CreateSolidBrush(ToColorRef(config_.layout.graphAxisColor));
    RECT verticalAxisRect{rect.left + axisWidth, rect.top, rect.left + axisWidth + strokeWidth, rect.bottom};
    RECT horizontalAxisRect{rect.left + axisWidth, rect.bottom - strokeWidth, rect.right, rect.bottom};
    FillRect(hdc, &verticalAxisRect, axisBrush);
    FillRect(hdc, &horizontalAxisRect, axisBrush);
    DeleteObject(axisBrush);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top, rect.left + axisWidth, graphTop};
    DrawTextBlock(hdc, maxRect, maxLabel, fonts_.smallFont, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleLogical(config_.layout.graphPlotStrokeWidth)), AccentColor());
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    for (size_t i = 1; i < history.size(); ++i) {
        const double v1 = std::clamp(history[i - 1] / maxValue, 0.0, 1.0);
        const double v2 = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x1 = graphLeft + static_cast<int>((i - 1) * width / std::max<size_t>(1, history.size() - 1));
        const int x2 = graphLeft + static_cast<int>(i * width / std::max<size_t>(1, history.size() - 1));
        const int y1 = rect.bottom - 1 - static_cast<int>(v1 * height);
        const int y2 = rect.bottom - 1 - static_cast<int>(v2 * height);
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardRenderer::DrawThroughputWidget(HDC hdc, const RECT& rect, const DashboardThroughputMetric& metric) {
    const int lineHeight = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.throughputValuePadding));
    RECT valueRect{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + lineHeight)};
    RECT graphRect{rect.left, std::min(rect.bottom, valueRect.bottom + std::max(0, ScaleLogical(config_.layout.throughputHeaderGap))),
        rect.right, rect.bottom};
    const int labelWidth = std::max(1, measuredWidths_.throughputLabel);
    RECT labelRect{valueRect.left, valueRect.top, std::min(valueRect.right, valueRect.left + labelWidth), valueRect.bottom};
    RECT numberRect{std::min(valueRect.right, labelRect.right + std::max(0, ScaleLogical(config_.layout.throughputHeaderGap))),
        valueRect.top, valueRect.right, valueRect.bottom};
    char buffer[64];
    if (metric.valueMbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", metric.valueMbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", metric.valueMbps);
    }
    DrawTextBlock(hdc, labelRect, metric.label, fonts_.smallFont, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, numberRect, buffer, fonts_.smallFont, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawGraph(hdc, graphRect, metric.history, metric.maxGraph, metric.guideStepMbps,
        metric.timeMarkerOffsetSamples, metric.timeMarkerIntervalSamples);
}

void DashboardRenderer::DrawDriveUsageWidget(HDC hdc, const RECT& rect, const std::vector<DashboardDriveRow>& rows) {
    const int rowHeight = EffectiveDriveRowHeight();
    RECT row{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + rowHeight)};
    for (const auto& drive : rows) {
        const int labelWidth = std::max(1, measuredWidths_.driveLabel);
        const int percentWidth = std::max(1, measuredWidths_.drivePercent);
        const int freeWidth = std::max(1, ScaleLogical(config_.layout.driveFreeWidth));
        const int barGap = std::max(0, ScaleLogical(config_.layout.driveBarGap));
        const int valueGap = std::max(0, ScaleLogical(config_.layout.driveValueGap));
        RECT labelRect{row.left, row.top, std::min(row.right, row.left + labelWidth), row.bottom};
        RECT pctRect{std::max(row.left, row.right - (percentWidth + freeWidth + valueGap)), row.top,
            std::max(row.left, row.right - (freeWidth + valueGap)), row.bottom};
        RECT freeRect{std::max(row.left, row.right - freeWidth), row.top, row.right, row.bottom};
        const int driveBarHeight = std::max(1, ScaleLogical(config_.layout.driveBarHeight));
        const int rowPixelHeight = static_cast<int>(row.bottom - row.top);
        const int barTop = static_cast<int>(row.top) + std::max(0, (rowPixelHeight - driveBarHeight) / 2);
        RECT barRect{
            labelRect.right + barGap,
            barTop,
            std::max(static_cast<int>(labelRect.right) + barGap, static_cast<int>(pctRect.left) - valueGap),
            std::min(static_cast<int>(row.bottom), barTop + driveBarHeight)
        };

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawPillBar(hdc, barRect, drive.usedPercent / 100.0, std::nullopt);

        char percent[16];
        sprintf_s(percent, "%.0f%%", drive.usedPercent);
        DrawTextBlock(hdc, pctRect, percent, fonts_.label, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        DrawTextBlock(hdc, freeRect, drive.freeText, fonts_.smallFont, MutedTextColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

        OffsetRect(&row, 0, rowHeight);
        row.bottom = std::min(rect.bottom, row.top + rowHeight);
        if (row.top >= rect.bottom) {
            break;
        }
    }
}

void DashboardRenderer::DrawResolvedWidget(HDC hdc, const ResolvedWidgetLayout& widget, const DashboardMetricSource& metrics) {
    switch (widget.kind) {
    case WidgetKind::Text:
        DrawTextBlock(hdc, widget.rect, metrics.ResolveText(widget.binding.metric), fonts_.label, ForegroundColor(),
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    case WidgetKind::Gauge: {
        const double percent = metrics.ResolveGaugePercent(widget.binding.metric);
        const int width = widget.rect.right - widget.rect.left;
        const int height = widget.rect.bottom - widget.rect.top;
        const int radius = std::max(
            std::max(1, ScaleLogical(config_.layout.gaugeMinRadius)),
            std::max(1, std::min(width, height) / 2 - std::max(0, ScaleLogical(config_.layout.gaugeOuterPadding))));
        DrawGauge(hdc, widget.rect.left + width / 2, widget.rect.top + height / 2, radius, percent, "Load");
        return;
    }
    case WidgetKind::MetricList: {
        const int rowHeight = EffectiveMetricRowHeight();
        RECT rowRect{widget.rect.left, widget.rect.top, widget.rect.right, std::min(widget.rect.bottom, widget.rect.top + rowHeight)};
        for (const auto& row : metrics.ResolveMetricList(ParseMetricListEntries(widget.binding.param))) {
            DrawMetricRow(hdc, rowRect, row);
            OffsetRect(&rowRect, 0, rowHeight);
            rowRect.bottom = std::min(widget.rect.bottom, rowRect.top + rowHeight);
            if (rowRect.top >= widget.rect.bottom) {
                break;
            }
        }
        return;
    }
    case WidgetKind::Throughput:
        DrawThroughputWidget(hdc, widget.rect, metrics.ResolveThroughput(widget.binding.metric));
        return;
    case WidgetKind::NetworkFooter:
        DrawTextBlock(hdc, widget.rect, metrics.ResolveNetworkFooter(), fonts_.smallFont, ForegroundColor(),
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    case WidgetKind::Spacer:
        return;
    case WidgetKind::DriveUsageList:
        DrawDriveUsageWidget(hdc, widget.rect, metrics.ResolveDriveRows(Split(widget.binding.param, ',')));
        return;
    case WidgetKind::ClockTime:
        DrawTextBlock(hdc, widget.rect, metrics.ResolveClockTime(), fonts_.big, ForegroundColor(),
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    case WidgetKind::ClockDate:
        DrawTextBlock(hdc, widget.rect, metrics.ResolveClockDate(), fonts_.value, MutedTextColor(),
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    default:
        return;
    }
}

void DashboardRenderer::Draw(HDC hdc, const SystemSnapshot& snapshot) {
    DashboardMetricSource metrics(snapshot);
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
