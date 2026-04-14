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

#include <gdiplus.h>

#include "../resources/resource.h"
#include "trace.h"
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

void FillCapsule(HDC hdc, const RECT& rect, COLORREF color, BYTE /*alpha*/) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    if (width <= height) {
        Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);
    } else {
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, height, height);
    }
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

bool CapsuleContainsSample(double sampleX, double sampleY, double width, double height) {
    if (width <= 0.0 || height <= 0.0) {
        return false;
    }

    if (width <= height) {
        const double centerX = width / 2.0;
        const double centerY = height / 2.0;
        const double radiusX = width / 2.0;
        const double radiusY = height / 2.0;
        const double dx = sampleX - centerX;
        const double dy = sampleY - centerY;
        return ((dx * dx) / (radiusX * radiusX)) + ((dy * dy) / (radiusY * radiusY)) <= 1.0;
    }

    const double radius = height / 2.0;
    const double leftCenterX = radius;
    const double rightCenterX = width - radius;
    const double centerY = radius;
    if (sampleX >= leftCenterX && sampleX <= rightCenterX && sampleY >= 0.0 && sampleY <= height) {
        return true;
    }

    const double dxLeft = sampleX - leftCenterX;
    const double dxRight = sampleX - rightCenterX;
    const double dy = sampleY - centerY;
    return (dxLeft * dxLeft) + (dy * dy) <= radius * radius || (dxRight * dxRight) + (dy * dy) <= radius * radius;
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

bool IsFastSingleLineTextFormat(const std::wstring& wideText, UINT format) {
    return !wideText.empty() && (format & DT_SINGLELINE) != 0 && (format & DT_END_ELLIPSIS) == 0 &&
           (format & DT_PATH_ELLIPSIS) == 0 && (format & DT_WORD_ELLIPSIS) == 0 && (format & DT_WORDBREAK) == 0 &&
           (format & DT_EDITCONTROL) == 0 && (format & DT_EXPANDTABS) == 0;
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
    HDC hdc, const RECT& rect, const std::string& text, HFONT font, UINT format) const {
    TextLayoutResult result{rect};
    const std::wstring& wideText = GetWideText(text);
    if (wideText.empty()) {
        return result;
    }

    UINT measureFormat = format | DT_CALCRECT;
    measureFormat &= ~DT_VCENTER;
    measureFormat &= ~DT_BOTTOM;
    measureFormat &= ~DT_NOCLIP;
    const int availableWidth = std::max(0, static_cast<int>(rect.right - rect.left));
    const int availableHeight = std::max(0, static_cast<int>(rect.bottom - rect.top));

    const TextMeasureCacheLookupKey cacheKey{font, text, measureFormat, availableWidth, availableHeight};
    SIZE measuredSize{};
    const auto cached = textMeasureCache_.find(cacheKey);
    if (cached != textMeasureCache_.end()) {
        measuredSize = cached->second;
    } else {
        HGDIOBJ oldFont = SelectObject(hdc, font);
        RECT measureRect{0, 0, availableWidth, availableHeight};
        ::DrawTextW(hdc, wideText.c_str(), -1, &measureRect, measureFormat);
        SelectObject(hdc, oldFont);
        measuredSize.cx = std::max(0, static_cast<int>(measureRect.right - measureRect.left));
        measuredSize.cy = std::max(0, static_cast<int>(measureRect.bottom - measureRect.top));
        textMeasureCache_.emplace(
            TextMeasureCacheKey{font, std::string(text), measureFormat, availableWidth, availableHeight}, measuredSize);
    }

    const int measuredWidth = measuredSize.cx;
    const int measuredHeight = measuredSize.cy;
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
    return result;
}

DashboardRenderer::TextLayoutResult DashboardRenderer::MeasureTextBlock(
    const RECT& rect, const std::string& text, HFONT font, UINT format) const {
    HDC hdc = GetDC(hwnd_ != nullptr ? hwnd_ : nullptr);
    if (hdc == nullptr) {
        return TextLayoutResult{rect};
    }

    const TextLayoutResult result = MeasureTextBlock(hdc, rect, text, font, format);
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    return result;
}

DashboardRenderer::TextLayoutResult DashboardRenderer::DrawTextBlock(
    HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) {
    TextLayoutResult result{rect};
    const std::wstring& wideText = GetWideText(text);
    if (wideText.empty()) {
        return result;
    }

    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    if (IsFastSingleLineTextFormat(wideText, format)) {
        const TextWidthCacheLookupKey cacheKey{font, text};
        SIZE size{};
        if (const auto it = textExtentCache_.find(cacheKey); it != textExtentCache_.end()) {
            size = it->second;
        } else {
            GetTextExtentPoint32W(hdc, wideText.c_str(), static_cast<int>(wideText.size()), &size);
            textExtentCache_.emplace(TextWidthCacheKey{font, text}, size);
        }

        int x = rect.left;
        if ((format & DT_CENTER) != 0) {
            x = rect.left + std::max(0L, (rect.right - rect.left - size.cx) / 2);
        } else if ((format & DT_RIGHT) != 0) {
            x = rect.right - size.cx;
        }

        int y = rect.top;
        if ((format & DT_VCENTER) != 0) {
            y = rect.top + std::max(0L, (rect.bottom - rect.top - size.cy) / 2);
        } else if ((format & DT_BOTTOM) != 0) {
            y = rect.bottom - size.cy;
        }

        result.textRect = RECT{x,
            y,
            std::min(rect.right, static_cast<LONG>(x + size.cx)),
            std::min(rect.bottom, static_cast<LONG>(y + size.cy))};

        const UINT options = (format & DT_NOCLIP) != 0 ? 0u : ETO_CLIPPED;
        const RECT* clipRect = (format & DT_NOCLIP) != 0 ? nullptr : &rect;
        ExtTextOutW(hdc, x, y, options, clipRect, wideText.c_str(), static_cast<UINT>(wideText.size()), nullptr);
    } else {
        result = MeasureTextBlock(hdc, rect, text, font, format);
        RECT copy = rect;
        ::DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    }
    SelectObject(hdc, oldFont);
    return result;
}

void DashboardRenderer::DrawText(
    HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) const {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    const std::wstring& wideText = GetWideText(text);
    const bool fastSingleLine = IsFastSingleLineTextFormat(wideText, format);
    if (fastSingleLine) {
        const TextWidthCacheLookupKey cacheKey{font, text};
        SIZE size{};
        if (const auto it = textExtentCache_.find(cacheKey); it != textExtentCache_.end()) {
            size = it->second;
        } else {
            GetTextExtentPoint32W(hdc, wideText.c_str(), static_cast<int>(wideText.size()), &size);
            textExtentCache_.emplace(TextWidthCacheKey{font, text}, size);
        }

        int x = rect.left;
        if ((format & DT_CENTER) != 0) {
            x = rect.left + std::max(0L, (rect.right - rect.left - size.cx) / 2);
        } else if ((format & DT_RIGHT) != 0) {
            x = rect.right - size.cx;
        }

        int y = rect.top;
        if ((format & DT_VCENTER) != 0) {
            y = rect.top + std::max(0L, (rect.bottom - rect.top - size.cy) / 2);
        } else if ((format & DT_BOTTOM) != 0) {
            y = rect.bottom - size.cy;
        }

        const UINT options = (format & DT_NOCLIP) != 0 ? 0u : ETO_CLIPPED;
        const RECT* clipRect = (format & DT_NOCLIP) != 0 ? nullptr : &rect;
        ExtTextOutW(hdc, x, y, options, clipRect, wideText.c_str(), static_cast<UINT>(wideText.size()), nullptr);
    } else {
        RECT copy = rect;
        ::DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    }
    SelectObject(hdc, oldFont);
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

    HPEN pen = SolidPen(LayoutGuideColor());
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(
        hdc, hoveredWidget->rect.left, hoveredWidget->rect.top, hoveredWidget->rect.right, hoveredWidget->rect.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

void DashboardRenderer::DrawHoveredEditableAnchorHighlight(HDC hdc, const EditOverlayState& overlayState) const {
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

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    for (const auto& [highlighted, active] : highlights) {
        const COLORREF outlineColor = active ? ActiveEditColor() : LayoutGuideColor();
        if (highlighted.drawTargetOutline && highlighted.targetRect.right > highlighted.targetRect.left &&
            highlighted.targetRect.bottom > highlighted.targetRect.top) {
            Gdiplus::Pen pen(
                Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)),
                static_cast<Gdiplus::REAL>(std::max(1, ScaleLogical(1))));
            pen.SetDashStyle(Gdiplus::DashStyleDot);
            const RECT& targetRect = highlighted.targetRect;
            graphics.DrawRectangle(&pen,
                static_cast<Gdiplus::REAL>(targetRect.left),
                static_cast<Gdiplus::REAL>(targetRect.top),
                static_cast<Gdiplus::REAL>(std::max<LONG>(1, targetRect.right - targetRect.left)),
                static_cast<Gdiplus::REAL>(std::max<LONG>(1, targetRect.bottom - targetRect.top)));
        }

        if (highlighted.shape == AnchorShape::Circle) {
            const RECT& anchorRect = highlighted.anchorRect;
            Gdiplus::Pen pen(
                Gdiplus::Color(255, GetRValue(outlineColor), GetGValue(outlineColor), GetBValue(outlineColor)),
                static_cast<Gdiplus::REAL>(std::max(1, ScaleLogical(1))));
            graphics.DrawEllipse(&pen,
                static_cast<Gdiplus::REAL>(anchorRect.left),
                static_cast<Gdiplus::REAL>(anchorRect.top),
                static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.right - anchorRect.left)),
                static_cast<Gdiplus::REAL>(std::max<LONG>(1, anchorRect.bottom - anchorRect.top)));
        } else {
            FillDiamond(hdc, highlighted.anchorRect, outlineColor);
        }
    }
}

void DashboardRenderer::DrawLayoutEditGuides(HDC hdc, const EditOverlayState& overlayState) const {
    if (!overlayState.showLayoutEditGuides || layoutEditGuides_.empty()) {
        return;
    }

    HPEN pen = SolidPen(LayoutGuideColor());
    HPEN activePen = SolidPen(ActiveEditColor());
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

    HPEN pen = SolidPen(LayoutGuideColor());
    HPEN activePen = SolidPen(ActiveEditColor());
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
    HDC measureHdc,
    HFONT font,
    UINT format,
    const EditableAnchorBinding& editable) {
    if (text.empty()) {
        return;
    }

    const TextLayoutResult result = measureHdc != nullptr ? MeasureTextBlock(measureHdc, rect, text, font, format)
                                                          : MeasureTextBlock(rect, text, font, format);
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
    RegisterTextAnchor(staticEditableAnchorRegions_, rect, text, staticAnchorMeasureHdc_, font, format, editable);
}

void DashboardRenderer::RegisterDynamicTextAnchor(HDC hdc,
    const RECT& rect,
    const std::string& text,
    HFONT font,
    UINT format,
    const EditableAnchorBinding& editable) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, rect, text, hdc, font, format, editable);
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
    RegisterTextAnchor(dynamicEditableAnchorRegions_, rect, text, nullptr, font, format, editable);
}

void DashboardRenderer::DrawAlphaCapsule(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    if (alpha == 255) {
        FillCapsule(hdc, rect, color, alpha);
        return;
    }

    const AlphaCapsuleCacheKey key{color, alpha, width, height};
    auto it = alphaCapsuleCache_.find(key);
    if (it == alphaCapsuleCache_.end()) {
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HDC sourceDc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(sourceDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (sourceDc == nullptr || bitmap == nullptr || pixels == nullptr) {
            if (bitmap != nullptr) {
                DeleteObject(bitmap);
            }
            if (sourceDc != nullptr) {
                DeleteDC(sourceDc);
            }
            return;
        }

        SelectObject(sourceDc, bitmap);
        auto* dst = static_cast<BYTE*>(pixels);
        const int stride = width * 4;
        constexpr int kSamplesPerAxis = 4;
        constexpr int kSampleCount = kSamplesPerAxis * kSamplesPerAxis;
        const std::array<double, kSamplesPerAxis> offsets{0.125, 0.375, 0.625, 0.875};
        const BYTE red = GetRValue(color);
        const BYTE green = GetGValue(color);
        const BYTE blue = GetBValue(color);
        const double alphaScale = static_cast<double>(alpha) / 255.0;
        for (int y = 0; y < height; ++y) {
            BYTE* row = dst + (y * stride);
            for (int x = 0; x < width; ++x) {
                int coveredSamples = 0;
                for (double offsetY : offsets) {
                    for (double offsetX : offsets) {
                        if (CapsuleContainsSample(
                                static_cast<double>(x) + offsetX, static_cast<double>(y) + offsetY, width, height)) {
                            ++coveredSamples;
                        }
                    }
                }

                const double coverage = static_cast<double>(coveredSamples) / kSampleCount;
                const BYTE sampleAlpha = static_cast<BYTE>(std::lround(255.0 * coverage * alphaScale));
                row[x * 4 + 0] = static_cast<BYTE>(std::lround(static_cast<double>(blue) * sampleAlpha / 255.0));
                row[x * 4 + 1] = static_cast<BYTE>(std::lround(static_cast<double>(green) * sampleAlpha / 255.0));
                row[x * 4 + 2] = static_cast<BYTE>(std::lround(static_cast<double>(red) * sampleAlpha / 255.0));
                row[x * 4 + 3] = sampleAlpha;
            }
        }

        it = alphaCapsuleCache_.emplace(key, AlphaCapsuleBitmap{sourceDc, bitmap}).first;
    }

    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, rect.left, rect.top, width, height, it->second.hdc, 0, 0, width, height, blend);
}

void DashboardRenderer::DrawLayoutSimilarityIndicators(HDC hdc, const EditOverlayState& overlayState) const {
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

    HPEN pen = SolidPen(color);
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
}

void DashboardRenderer::DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect) {
    const auto it = std::find_if(
        panelIcons_.begin(), panelIcons_.end(), [&](const auto& entry) { return entry.first == iconName; });
    if (it == panelIcons_.end() || it->second == nullptr) {
        return;
    }
    const int width = std::max(0, static_cast<int>(iconRect.right - iconRect.left));
    const int height = std::max(0, static_cast<int>(iconRect.bottom - iconRect.top));
    if (width <= 0 || height <= 0) {
        return;
    }

    const PanelIconCacheKey cacheKey{iconName, width, height};
    auto scaled = scaledPanelIconCache_.find(cacheKey);
    if (scaled == scaledPanelIconCache_.end()) {
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

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HDC sourceDc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(sourceDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (sourceDc == nullptr || bitmap == nullptr || pixels == nullptr) {
            surface.UnlockBits(&bitmapData);
            if (bitmap != nullptr) {
                DeleteObject(bitmap);
            }
            if (sourceDc != nullptr) {
                DeleteDC(sourceDc);
            }
            return;
        }
        SelectObject(sourceDc, bitmap);
        for (int y = 0; y < height; ++y) {
            memcpy(static_cast<BYTE*>(pixels) + (y * width * 4),
                static_cast<const BYTE*>(bitmapData.Scan0) + (y * bitmapData.Stride),
                static_cast<size_t>(width) * 4);
        }
        surface.UnlockBits(&bitmapData);
        scaled = scaledPanelIconCache_.emplace(cacheKey, AlphaCapsuleBitmap{sourceDc, bitmap}).first;
    }

    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, iconRect.left, iconRect.top, width, height, scaled->second.hdc, 0, 0, width, height, blend);
}

void DashboardRenderer::DrawPanel(HDC hdc, const ResolvedCardLayout& card) {
    HPEN border = SolidPen(ToColorRef(config_.layout.colors.panelBorderColor),
        std::max(1, ScaleLogical(config_.layout.cardStyle.cardBorderWidth)));
    HBRUSH fill = SolidBrush(ToColorRef(config_.layout.colors.panelFillColor));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    const int radius = std::max(1, ScaleLogical(config_.layout.cardStyle.cardRadius));
    RoundRect(hdc, card.rect.left, card.rect.top, card.rect.right, card.rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    if (!card.iconName.empty()) {
        DrawPanelIcon(hdc, card.iconName, card.iconRect);
    }
    if (!card.title.empty()) {
        DrawText(
            hdc, card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

void DashboardRenderer::DrawPillBar(
    HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    const auto fillOpaqueCapsule = [&](const RECT& capsuleRect, COLORREF color) {
        const int width = std::max(0, static_cast<int>(capsuleRect.right - capsuleRect.left));
        const int height = std::max(0, static_cast<int>(capsuleRect.bottom - capsuleRect.top));
        if (width <= 0 || height <= 0) {
            return;
        }

        HGDIOBJ oldBrush = SelectObject(hdc, SolidBrush(color));
        HGDIOBJ oldPen = SelectObject(hdc, SolidPen(color));
        if (width <= height) {
            Ellipse(hdc, capsuleRect.left, capsuleRect.top, capsuleRect.right, capsuleRect.bottom);
        } else {
            RoundRect(hdc, capsuleRect.left, capsuleRect.top, capsuleRect.right, capsuleRect.bottom, height, height);
        }
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
    };

    const COLORREF accentColor = AccentColor();
    fillOpaqueCapsule(rect, ToColorRef(config_.layout.colors.trackColor));

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
    fillOpaqueCapsule(fillRect, accentColor);

    if (peakRatio.has_value()) {
        const double peak = std::clamp(*peakRatio, 0.0, 1.0);
        const int markerWidth = std::min(width, std::max(1, std::max(ScaleLogical(4), height)));
        const int centerX = static_cast<int>(rect.left) + static_cast<int>(std::round(peak * width));
        const int minLeft = static_cast<int>(rect.left);
        const int maxLeft = static_cast<int>(rect.right) - markerWidth;
        const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
        RECT markerRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
        DrawAlphaCapsule(hdc, markerRect, accentColor, 96);
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
    dynamicEditableAnchorRegions_.clear();
    dynamicAnchorRegistrationEnabled_ =
        overlayState.showLayoutEditGuides && !overlayState.activeLayoutEditGuide.has_value();
    const DashboardMetricSource& metrics = ResolveMetrics(snapshot);
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
    dynamicAnchorRegistrationEnabled_ = false;
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
    HBRUSH background = SolidBrush(BackgroundColor());
    FillRect(memDc, &client, background);
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

int DashboardRenderer::MeasureTextWidth(HFONT font, std::string_view text) const {
    const TextWidthCacheLookupKey cacheKey{font, text};
    if (const auto it = textWidthCache_.find(cacheKey); it != textWidthCache_.end()) {
        return it->second;
    }

    HDC hdc = GetDC(hwnd_ != nullptr ? hwnd_ : nullptr);
    if (hdc == nullptr) {
        return 0;
    }

    const std::wstring& wideText = GetWideText(text);
    SIZE size{};
    HGDIOBJ oldFont = SelectObject(hdc, font);
    if (!wideText.empty()) {
        GetTextExtentPoint32W(hdc, wideText.c_str(), static_cast<int>(wideText.size()), &size);
    }
    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    textWidthCache_.emplace(TextWidthCacheKey{font, std::string(text)}, size.cx);
    return size.cx;
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
    InvalidateMetricSourceCache();
    DestroyFonts();
    fontHeights_ = {};
    resolvedLayout_ = {};
    parsedWidgetInfoCache_.clear();
    wideTextCache_.clear();
    textWidthCache_.clear();
    textExtentCache_.clear();
    textMeasureCache_.clear();
    staticEditableAnchorRegions_.clear();
    dynamicEditableAnchorRegions_.clear();
    dynamicAnchorRegistrationEnabled_ = false;
    ClearGdiCaches();
    ReleasePanelIcons();
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
    wideTextCache_.clear();
    textWidthCache_.clear();
    textExtentCache_.clear();
    textMeasureCache_.clear();
}

HBRUSH DashboardRenderer::SolidBrush(COLORREF color) const {
    if (const auto it = solidBrushCache_.find(color); it != solidBrushCache_.end()) {
        return it->second;
    }

    HBRUSH brush = CreateSolidBrush(color);
    solidBrushCache_.emplace(color, brush);
    return brush;
}

HPEN DashboardRenderer::SolidPen(COLORREF color, int width) const {
    const PenCacheKey key{color, std::max(1, width)};
    if (const auto it = solidPenCache_.find(key); it != solidPenCache_.end()) {
        return it->second;
    }

    HPEN pen = CreatePen(PS_SOLID, key.width, color);
    solidPenCache_.emplace(key, pen);
    return pen;
}

const std::wstring& DashboardRenderer::GetWideText(std::string_view text) const {
    if (const auto it = wideTextCache_.find(text); it != wideTextCache_.end()) {
        return it->second;
    }

    std::string key(text);
    return wideTextCache_.emplace(key, WideFromUtf8(key)).first->second;
}

void DashboardRenderer::ClearGdiCaches() {
    for (const auto& entry : solidBrushCache_) {
        DeleteObject(entry.second);
    }
    solidBrushCache_.clear();
    for (const auto& entry : solidPenCache_) {
        DeleteObject(entry.second);
    }
    solidPenCache_.clear();
    for (const auto& entry : alphaCapsuleCache_) {
        DeleteObject(entry.second.bitmap);
        DeleteDC(entry.second.hdc);
    }
    alphaCapsuleCache_.clear();
    for (const auto& entry : scaledPanelIconCache_) {
        DeleteObject(entry.second.bitmap);
        DeleteDC(entry.second.hdc);
    }
    scaledPanelIconCache_.clear();
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
    for (const auto& entry : scaledPanelIconCache_) {
        DeleteObject(entry.second.bitmap);
        DeleteDC(entry.second.hdc);
    }
    scaledPanelIconCache_.clear();
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
