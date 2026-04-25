#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "renderer/render_types.h"

class Trace;

enum class RenderMode {
    Normal,
    Blank,
};

struct TextStyleMetrics {
    int title = 0;
    int big = 0;
    int value = 0;
    int label = 0;
    int text = 0;
    int smallText = 0;
    int footer = 0;
    int clockTime = 0;
    int clockDate = 0;
};

struct TextLayoutResult {
    RenderRect textRect{};
};

struct RendererStyle {
    ColorsConfig colors;
    UiFontSetConfig fonts;
    std::vector<std::string> iconNames;
    double scale = 1.0;
};

class Renderer {
public:
    using DrawCallback = std::function<void()>;

    virtual ~Renderer() = default;

    virtual bool SetStyle(const RendererStyle& style) = 0;
    virtual void AttachWindow(HWND hwnd) = 0;
    virtual void Shutdown() = 0;
    virtual void SetImmediatePresent(bool enabled) = 0;
    virtual void SetTraceSuppressed(bool suppressed) = 0;
    virtual void DiscardWindowTarget(std::string_view reason = {}) = 0;
    virtual bool DrawWindow(int width, int height, const DrawCallback& draw) = 0;
    virtual bool DrawOffscreen(int width, int height, const DrawCallback& draw) = 0;
    virtual bool SavePng(const std::filesystem::path& imagePath, int width, int height, const DrawCallback& draw) = 0;
    virtual const std::string& LastError() const = 0;
    virtual const TextStyleMetrics& TextMetrics() const = 0;
    virtual int ScaleLogical(int value) const = 0;
    virtual int MeasureTextWidth(TextStyleId style, std::string_view text) const = 0;
    virtual TextLayoutResult MeasureTextBlock(
        const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const = 0;
    virtual void DrawText(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) const = 0;
    virtual TextLayoutResult DrawTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) = 0;
    virtual void PushClipRect(const RenderRect& rect) = 0;
    virtual void PopClipRect() = 0;
    virtual void PushTranslation(RenderPoint offset) = 0;
    virtual void PopTranslation() = 0;
    virtual bool DrawIcon(std::string_view iconName, const RenderRect& rect) = 0;
    virtual bool FillSolidRect(const RenderRect& rect, RenderColorId color) = 0;
    virtual bool FillSolidRoundedRect(const RenderRect& rect, int radius, RenderColorId color) = 0;
    virtual bool FillSolidEllipse(const RenderRect& rect, RenderColorId color) = 0;
    virtual bool FillSolidDiamond(const RenderRect& rect, RenderColorId color) = 0;
    virtual bool DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke) = 0;
    virtual bool DrawSolidRoundedRect(const RenderRect& rect, int radius, const RenderStroke& stroke) = 0;
    virtual bool DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke) = 0;
    virtual bool DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke) = 0;
    virtual bool DrawArc(const RenderArc& arc, const RenderStroke& stroke) = 0;
    virtual bool DrawArcs(std::span<const RenderArc> arcs, const RenderStroke& stroke) = 0;
    virtual bool DrawPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke) = 0;
    virtual bool FillPath(const RenderPath& path, RenderColorId color) = 0;
    virtual bool FillPaths(std::span<const RenderPath> paths, RenderColorId color) = 0;
};

std::unique_ptr<Renderer> CreateRenderer(Trace& trace);
