#pragma once

#include <windows.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config/config_def.h"
#include "renderer/render_types.h"
#include "util/file_path.h"
#include "util/function_ref.h"

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
    LayoutGuideSheetConfig layoutGuideSheet;
    FontsConfig fonts;
    std::vector<std::string> iconNames;
    double scale = 1.0;
};

class RenderBitmapResource {
public:
    virtual ~RenderBitmapResource() = default;

    virtual const void* TypeToken() const = 0;
};

enum class RenderBitmapStorage {
    Generic,
    LiveLayer,
};

struct RenderBitmap {
    int width = 0;
    int height = 0;
    RenderBitmapStorage storage = RenderBitmapStorage::Generic;
    std::shared_ptr<RenderBitmapResource> resource;

    bool Empty() const;
    bool IsLiveLayer() const;
};

enum class RenderBitmapClear {
    Transparent,
    Background,
};

class Renderer {
public:
    using DrawCallback = FunctionRef<void()>;
    using DirtyDrawCallback = FunctionRef<void(std::span<const RenderRect>)>;

    virtual ~Renderer() = default;

    virtual bool SetStyle(const RendererStyle& style) = 0;
    virtual void AttachWindow(HWND hwnd) = 0;
    virtual void Shutdown() = 0;
    virtual void SetImmediatePresent(bool enabled) = 0;
    virtual void DiscardWindowTarget(std::string_view reason = {}) = 0;
    virtual bool DrawWindow(int width, int height, const DrawCallback& draw) = 0;
    virtual bool DrawWindowRetained(int width, int height, const DrawCallback& draw) = 0;
    virtual bool DrawWindowDirty(
        int width, int height, std::span<const RenderRect> dirtyRects, const DirtyDrawCallback& draw) = 0;
    virtual bool DrawOffscreen(int width, int height, const DrawCallback& draw) = 0;
    virtual bool DrawToBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear clear, const DrawCallback& draw) = 0;
    virtual bool DrawToLiveLayerBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear clear, const DrawCallback& draw) = 0;
    virtual bool SavePng(const FilePath& imagePath, int width, int height, const DrawCallback& draw) = 0;
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
    virtual bool DrawBitmap(const RenderBitmap& bitmap, RenderPoint origin) = 0;
    virtual bool DrawBitmapRegion(
        const RenderBitmap& bitmap, const RenderRect& sourceRect, RenderPoint targetOrigin) = 0;
    virtual bool DrawBitmapRegions(const RenderBitmap& bitmap, std::span<const RenderRect> sourceRects) = 0;
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

std::unique_ptr<Renderer> CreateRenderer();
