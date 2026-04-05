#include "dashboard_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <objidl.h>
#include <optional>
#include <set>
#include <sstream>

#include <gdiplus.h>

#include "../resources/resource.h"
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

std::optional<std::string> GetNodeParameter(const LayoutNodeConfig& node, const std::string& key) {
    for (const auto& parameter : node.parameters) {
        if (ToLower(parameter.first) == ToLower(key)) {
            return parameter.second;
        }
    }
    return std::nullopt;
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

}  // namespace

DashboardRenderer::DashboardRenderer() = default;

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

void DashboardRenderer::SetConfig(const AppConfig& config) {
    config_ = config;
}

int DashboardRenderer::WindowWidth() const {
    return std::max(1, config_.layout.windowWidth);
}

int DashboardRenderer::WindowHeight() const {
    return std::max(1, config_.layout.windowHeight);
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

bool DashboardRenderer::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    if (!InitializeGdiplus() || !LoadPanelIcons()) {
        return false;
    }
    if (fonts_.title == nullptr) {
        fonts_.title = CreateUiFont(config_.layout.titleFont);
        fonts_.big = CreateUiFont(config_.layout.bigFont);
        fonts_.value = CreateUiFont(config_.layout.valueFont);
        fonts_.label = CreateUiFont(config_.layout.labelFont);
        fonts_.smallFont = CreateUiFont(config_.layout.smallFont);
    }
    if (fonts_.title == nullptr || fonts_.big == nullptr || fonts_.value == nullptr ||
        fonts_.label == nullptr || fonts_.smallFont == nullptr) {
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
    resolvedLayout_ = {};
    ReleasePanelIcons();
    ShutdownGdiplus();
}

bool DashboardRenderer::InitializeGdiplus() {
    if (gdiplusToken_ != 0) {
        return true;
    }
    Gdiplus::GdiplusStartupInput startupInput;
    return Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) == Gdiplus::Ok;
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
            ReleasePanelIcons();
            return false;
        }
        auto bitmap = LoadPngResourceBitmap(resourceId);
        if (bitmap == nullptr) {
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

bool DashboardRenderer::MeasureFonts() {
    HDC hdc = GetDC(hwnd_ != nullptr ? hwnd_ : nullptr);
    if (hdc == nullptr) {
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
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    return true;
}

int DashboardRenderer::EffectiveHeaderHeight() const {
    const int titleHeight = std::max(fontHeights_.title, config_.layout.headerIconSize);
    return std::max(config_.layout.headerHeight, titleHeight);
}

int DashboardRenderer::EffectiveMetricRowHeight() const {
    return std::max(config_.layout.metricRowHeight,
        std::max(fontHeights_.label, fontHeights_.value) + std::max(8, config_.layout.widgetLineGap));
}

int DashboardRenderer::PreferredNodeHeight(const LayoutNodeConfig& node, int) const {
    const std::string lowered = ToLower(node.name);
    if (lowered == "stack_top") {
        int total = 0;
        for (size_t i = 0; i < node.children.size(); ++i) {
            total += PreferredNodeHeight(node.children[i], 0);
            if (i + 1 < node.children.size()) {
                total += config_.layout.widgetLineGap;
            }
        }
        return total;
    }
    if (lowered == "text" || lowered == "cpu_name" || lowered == "gpu_name") {
        return fontHeights_.label + 2;
    }
    if (lowered == "network_footer") {
        return fontHeights_.smallText + 2;
    }
    if (lowered == "metric_list" || lowered == "metric_list_cpu" || lowered == "metric_list_gpu") {
        const int count = std::max<int>(1, static_cast<int>(GetNodeParameter(node, "items").has_value()
            ? Split(*GetNodeParameter(node, "items"), ',') .size() : 4));
        return count * EffectiveMetricRowHeight();
    }
    if (lowered == "drive_usage_list") {
        const int count = std::max<int>(1, static_cast<int>(GetNodeParameter(node, "drives").has_value()
            ? Split(*GetNodeParameter(node, "drives"), ',').size() : 3));
        return count * std::max(1, config_.layout.driveRowHeight);
    }
    if (lowered == "throughput" || lowered == "throughput_upload" || lowered == "throughput_download" ||
        lowered == "throughput_read" || lowered == "throughput_write") {
        return fontHeights_.smallText + config_.layout.throughputHeaderGap +
            std::max(1, config_.layout.throughputGraphHeight);
    }
    if (lowered == "clock_time") {
        return fontHeights_.big + 8;
    }
    if (lowered == "clock_date") {
        return fontHeights_.value + 6;
    }
    if (lowered == "gauge" || lowered == "gauge_cpu_load" || lowered == "gauge_gpu_load") {
        return std::max(1, config_.layout.gaugePreferredSize);
    }
    return 0;
}

void DashboardRenderer::ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets) {
    if (!IsContainerNode(node)) {
        ResolvedWidgetLayout widget;
        const std::string lowered = ToLower(node.name);
        if (lowered == "text") {
            widget.kind = WidgetKind::Text;
            if (const auto value = GetNodeParameter(node, "value"); value.has_value()) {
                widget.binding.metric = *value;
            }
        } else if (lowered == "gauge") {
            widget.kind = WidgetKind::Gauge;
            if (const auto value = GetNodeParameter(node, "value"); value.has_value()) {
                widget.binding.metric = *value;
            }
        } else if (lowered == "metric_list") {
            widget.kind = WidgetKind::MetricList;
            if (const auto items = GetNodeParameter(node, "items"); items.has_value()) {
                widget.binding.items = Split(*items, ',');
            }
        } else if (lowered == "throughput") {
            widget.kind = WidgetKind::Throughput;
            if (const auto value = GetNodeParameter(node, "value"); value.has_value()) {
                widget.binding.metric = *value;
            }
        } else if (lowered == "network_footer") {
            widget.kind = WidgetKind::NetworkFooter;
        } else if (lowered == "spacer") {
            widget.kind = WidgetKind::Spacer;
        } else if (lowered == "drive_usage_list") {
            widget.kind = WidgetKind::DriveUsageList;
            if (const auto drives = GetNodeParameter(node, "drives"); drives.has_value()) {
                widget.binding.drives = Split(*drives, ',');
            }
        } else if (lowered == "clock_time") {
            widget.kind = WidgetKind::ClockTime;
        } else if (lowered == "clock_date") {
            widget.kind = WidgetKind::ClockDate;
        } else if (lowered == "cpu_name") {
            widget.kind = WidgetKind::Text;
            widget.binding.metric = "cpu.name";
        } else if (lowered == "gpu_name") {
            widget.kind = WidgetKind::Text;
            widget.binding.metric = "gpu.name";
        } else if (lowered == "gauge_cpu_load") {
            widget.kind = WidgetKind::Gauge;
            widget.binding.metric = "cpu.load";
        } else if (lowered == "gauge_gpu_load") {
            widget.kind = WidgetKind::Gauge;
            widget.binding.metric = "gpu.load";
        } else if (lowered == "throughput_upload") {
            widget.kind = WidgetKind::Throughput;
            widget.binding.metric = "network.upload";
        } else if (lowered == "throughput_download") {
            widget.kind = WidgetKind::Throughput;
            widget.binding.metric = "network.download";
        } else if (lowered == "throughput_read") {
            widget.kind = WidgetKind::Throughput;
            widget.binding.metric = "storage.read";
        } else if (lowered == "throughput_write") {
            widget.kind = WidgetKind::Throughput;
            widget.binding.metric = "storage.write";
        } else if (lowered == "metric_list_cpu" || lowered == "metric_list_gpu") {
            widget.kind = WidgetKind::MetricList;
            if (const auto items = GetNodeParameter(node, "items"); items.has_value()) {
                const std::string prefix = lowered == "metric_list_cpu" ? "cpu." : "gpu.";
                for (const auto& item : Split(*items, ',')) {
                    widget.binding.items.push_back(prefix + item);
                }
            }
        }
        widget.rect = rect;
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = ToLower(node.name) == "columns";
    const bool topPacked = ToLower(node.name) == "stack_top";
    const int gap = horizontal ? config_.layout.columnGap : config_.layout.widgetLineGap;
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
        config_.layout.outerMargin,
        config_.layout.outerMargin,
        WindowWidth() - config_.layout.outerMargin,
        WindowHeight() - config_.layout.outerMargin
    };

    int totalRowWeight = 0;
    for (const auto& row : config_.layout.rows) {
        totalRowWeight += std::max(1, row.weight);
    }
    if (totalRowWeight <= 0) {
        return false;
    }

    const int rowGap = config_.layout.rowGap;
    const int totalHeight = (dashboardRect.bottom - dashboardRect.top) -
        rowGap * static_cast<int>(std::max<size_t>(0, config_.layout.rows.size() - 1));
    int remainingHeight = totalHeight;
    int rowTop = dashboardRect.top;
    int remainingRowWeight = totalRowWeight;

    for (size_t rowIndex = 0; rowIndex < config_.layout.rows.size(); ++rowIndex) {
        const auto& row = config_.layout.rows[rowIndex];
        const int rowWeight = std::max(1, row.weight);
        const int rowHeight = (rowIndex + 1 == config_.layout.rows.size())
            ? (dashboardRect.bottom - rowTop)
            : std::max(0, remainingHeight * rowWeight / remainingRowWeight);

        int totalCardWeight = 0;
        for (const auto& cardRef : row.cards) {
            totalCardWeight += std::max(1, cardRef.weight);
        }

        const int cardGap = config_.layout.cardGap;
        const int totalWidth = (dashboardRect.right - dashboardRect.left) -
            cardGap * static_cast<int>(std::max<size_t>(0, row.cards.size() - 1));
        int remainingWidth = totalWidth;
        int cardLeft = dashboardRect.left;
        int remainingCardWeight = std::max(1, totalCardWeight);

        for (size_t cardIndex = 0; cardIndex < row.cards.size(); ++cardIndex) {
            const auto& cardRef = row.cards[cardIndex];
            const auto cardIt = std::find_if(config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) {
                return ToLower(card.id) == ToLower(cardRef.cardId);
            });
            if (cardIt == config_.layout.cards.end()) {
                continue;
            }

            const int cardWeight = std::max(1, cardRef.weight);
            const int cardWidth = (cardIndex + 1 == row.cards.size())
                ? (dashboardRect.right - cardLeft)
                : std::max(0, remainingWidth * cardWeight / remainingCardWeight);

            ResolvedCardLayout card;
            card.id = cardIt->id;
            card.title = cardIt->title;
            card.iconName = cardIt->icon;
            card.rect = RECT{cardLeft, rowTop, cardLeft + cardWidth, rowTop + rowHeight};

            const int padding = config_.layout.cardPadding;
            const int headerHeight = EffectiveHeaderHeight();
            card.iconRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + std::max(0, (headerHeight - config_.layout.headerIconSize) / 2),
                card.rect.left + padding + config_.layout.headerIconSize,
                card.rect.top + padding + std::max(0, (headerHeight - config_.layout.headerIconSize) / 2) + config_.layout.headerIconSize
            };
            card.titleRect = RECT{
                card.iconRect.right + config_.layout.headerGap,
                card.rect.top + padding,
                card.rect.right - padding,
                card.rect.top + padding + headerHeight
            };
            card.contentRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + headerHeight + config_.layout.contentGap,
                card.rect.right - padding,
                card.rect.bottom - padding
            };

            ResolveNodeWidgets(cardIt->layout, card.contentRect, card.widgets);
            resolvedLayout_.cards.push_back(std::move(card));

            cardLeft += cardWidth + cardGap;
            remainingWidth -= cardWidth;
            remainingCardWeight -= cardWeight;
        }

        rowTop += rowHeight + rowGap;
        remainingHeight -= rowHeight;
        remainingRowWeight -= rowWeight;
    }

    return !resolvedLayout_.cards.empty();
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
    HPEN border = CreatePen(PS_SOLID, std::max(1, config_.layout.cardBorderWidth), ToColorRef(config_.layout.panelBorderColor));
    HBRUSH fill = CreateSolidBrush(ToColorRef(config_.layout.panelFillColor));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    const int radius = std::max(1, config_.layout.cardRadius);
    RoundRect(hdc, card.rect.left, card.rect.top, card.rect.right, card.rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(border);
    DrawPanelIcon(hdc, card.iconName, card.iconRect);
    DrawTextBlock(hdc, card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

void DashboardRenderer::DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label) {
    HPEN trackPen = CreatePen(PS_SOLID, 10, ToColorRef(config_.layout.trackColor));
    HPEN usagePen = CreatePen(PS_SOLID, 10, ToColorRef(config_.layout.accentColor));
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

    RECT numberRect{cx - 42, cy - 28, cx + 42, cy + 18};
    char number[16];
    sprintf_s(number, "%.0f%%", percent);
    DrawTextBlock(hdc, numberRect, number, fonts_.big, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT labelRect{cx - 42, cy + 18, cx + 42, cy + 42};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DashboardRenderer::DrawMetricRow(HDC hdc, const RECT& rect, const DashboardMetricRow& row) {
    const int labelWidth = std::max(1, config_.layout.metricLabelWidth);
    const int valueGap = std::max(0, config_.layout.metricValueGap);
    RECT labelRect{rect.left, rect.top, std::min(rect.right, rect.left + labelWidth), rect.bottom};
    RECT valueRect{std::min(rect.right, labelRect.right + valueGap), rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, row.label, fonts_.label, MutedTextColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, valueRect, row.valueText, fonts_.value, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const int metricBarHeight = std::max(1, config_.layout.metricBarHeight);
    const int barBottom = std::min(static_cast<int>(rect.bottom), static_cast<int>(rect.top) + EffectiveMetricRowHeight());
    const int barTop = std::max(static_cast<int>(rect.top), barBottom - metricBarHeight);
    RECT barRect{valueRect.left, barTop, rect.right, barBottom};
    HBRUSH track = CreateSolidBrush(ToColorRef(config_.layout.trackColor));
    FillRect(hdc, &barRect, track);
    DeleteObject(track);

    RECT fill = barRect;
    fill.right = fill.left + static_cast<int>((fill.right - fill.left) * std::clamp(row.ratio, 0.0, 1.0));
    HBRUSH accent = CreateSolidBrush(ToColorRef(config_.layout.accentColor));
    FillRect(hdc, &fill, accent);
    DeleteObject(accent);
}

void DashboardRenderer::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue) {
    HBRUSH bg = CreateSolidBrush(ToColorRef(config_.layout.graphBackgroundColor));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    const int axisWidth = std::max(1, config_.layout.throughputAxisWidth);
    const int graphLeft = rect.left + axisWidth;
    const int width = std::max<int>(1, rect.right - graphLeft - 1);
    const int height = std::max<int>(1, rect.bottom - rect.top - 1);
    const int graphRight = graphLeft + width;
    const int graphBottom = rect.bottom - 1;

    HPEN gridPen = CreatePen(PS_SOLID, 1, ToColorRef(config_.layout.graphGridColor));
    HGDIOBJ oldPen = SelectObject(hdc, gridPen);
    for (double tick = 5.0; tick < maxValue; tick += 5.0) {
        const double ratio = tick / maxValue;
        const int y = graphBottom - static_cast<int>(std::round(ratio * height));
        MoveToEx(hdc, graphLeft, y, nullptr);
        LineTo(hdc, graphRight, y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    HPEN axisPen = CreatePen(PS_SOLID, 1, ToColorRef(config_.layout.graphAxisColor));
    oldPen = SelectObject(hdc, axisPen);
    MoveToEx(hdc, rect.left + axisWidth, rect.top, nullptr);
    LineTo(hdc, rect.left + axisWidth, rect.bottom - 1);
    MoveToEx(hdc, rect.left + axisWidth, rect.bottom - 1, nullptr);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(axisPen);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top + 1, rect.left + axisWidth, rect.top + 13};
    DrawTextBlock(hdc, maxRect, maxLabel, fonts_.smallFont, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_TOP);

    HPEN pen = CreatePen(PS_SOLID, 2, AccentColor());
    oldPen = SelectObject(hdc, pen);
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
    const int lineHeight = fontHeights_.smallText + 2;
    RECT valueRect{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + lineHeight)};
    RECT graphRect{rect.left, std::min(rect.bottom, valueRect.bottom + std::max(0, config_.layout.throughputHeaderGap)), rect.right, rect.bottom};
    const int labelWidth = metric.label == "Write"
        ? std::max(1, config_.layout.throughputWriteLabelWidth)
        : std::max(1, config_.layout.throughputReadLabelWidth);
    RECT labelRect{valueRect.left, valueRect.top, std::min(valueRect.right, valueRect.left + labelWidth), valueRect.bottom};
    RECT numberRect{std::min(valueRect.right, labelRect.right + std::max(0, config_.layout.throughputHeaderGap)), valueRect.top, valueRect.right, valueRect.bottom};
    char buffer[64];
    if (metric.valueMbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", metric.valueMbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", metric.valueMbps);
    }
    DrawTextBlock(hdc, labelRect, metric.label, fonts_.smallFont, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, numberRect, buffer, fonts_.smallFont, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawGraph(hdc, graphRect, metric.history, metric.maxGraph);
}

void DashboardRenderer::DrawDriveUsageWidget(HDC hdc, const RECT& rect, const std::vector<DashboardDriveRow>& rows) {
    const int rowHeight = std::max(1, config_.layout.driveRowHeight);
    RECT row{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + rowHeight)};
    for (const auto& drive : rows) {
        const int labelWidth = std::max(1, config_.layout.driveLabelWidth);
        const int percentWidth = std::max(1, config_.layout.drivePercentWidth);
        const int freeWidth = std::max(1, config_.layout.driveFreeWidth);
        const int barGap = std::max(0, config_.layout.driveBarGap);
        const int valueGap = std::max(0, config_.layout.driveValueGap);
        RECT labelRect{row.left, row.top, std::min(row.right, row.left + labelWidth), row.bottom};
        RECT pctRect{std::max(row.left, row.right - (percentWidth + freeWidth + valueGap)), row.top,
            std::max(row.left, row.right - (freeWidth + valueGap)), row.bottom};
        RECT freeRect{std::max(row.left, row.right - freeWidth), row.top, row.right, row.bottom};
        const int driveBarHeight = std::max(2, config_.layout.driveBarHeight);
        const int rowPixelHeight = static_cast<int>(row.bottom - row.top);
        const int barTop = static_cast<int>(row.top) + std::max(0, (rowPixelHeight - driveBarHeight) / 2);
        RECT barRect{
            labelRect.right + barGap,
            barTop,
            std::max(static_cast<int>(labelRect.right) + barGap, static_cast<int>(pctRect.left) - valueGap),
            std::min(static_cast<int>(row.bottom), barTop + driveBarHeight)
        };

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        HBRUSH track = CreateSolidBrush(ToColorRef(config_.layout.trackColor));
        FillRect(hdc, &barRect, track);
        DeleteObject(track);

        RECT fill = barRect;
        fill.right = fill.left + static_cast<int>((fill.right - fill.left) * std::clamp(drive.usedPercent / 100.0, 0.0, 1.0));
        HBRUSH accent = CreateSolidBrush(ToColorRef(config_.layout.accentColor));
        FillRect(hdc, &fill, accent);
        DeleteObject(accent);

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
        const int radius = std::max(20, std::min(width, height) / 3);
        DrawGauge(hdc, widget.rect.left + width / 2, widget.rect.top + height / 2, radius, percent, "Load");
        return;
    }
    case WidgetKind::MetricList: {
        const int rowHeight = EffectiveMetricRowHeight();
        RECT rowRect{widget.rect.left, widget.rect.top, widget.rect.right, std::min(widget.rect.bottom, widget.rect.top + rowHeight)};
        for (const auto& row : metrics.ResolveMetricList(widget.binding.items)) {
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
        DrawDriveUsageWidget(hdc, widget.rect, metrics.ResolveDriveRows(widget.binding.drives));
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
        return false;
    }
    HDC memDc = CreateCompatibleDC(screenDc);
    if (memDc == nullptr) {
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
    const bool saved = GetImageEncoderClsid(L"image/png", &pngClsid) >= 0 &&
        image.Save(imagePath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return saved;
}
