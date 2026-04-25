#pragma once

#include <windows.h>

#include <array>
#include <d2d1.h>
#include <dwrite.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

#include "renderer/impl/d2d_cache.h"
#include "renderer/impl/palette.h"
#include "renderer/impl/text_width_cache.h"
#include "renderer/renderer.h"

class D2DRenderer final : public Renderer {
public:
    D2DRenderer();
    ~D2DRenderer() override;

    bool SetStyle(const RendererStyle& style) override;
    void AttachWindow(HWND hwnd) override;
    void Shutdown() override;
    void SetImmediatePresent(bool enabled) override;
    void DiscardWindowTarget(std::string_view reason = {}) override;
    bool DrawWindow(int width, int height, const DrawCallback& draw) override;
    bool DrawOffscreen(int width, int height, const DrawCallback& draw) override;
    bool SavePng(const std::filesystem::path& imagePath, int width, int height, const DrawCallback& draw) override;
    const std::string& LastError() const override;
    const TextStyleMetrics& TextMetrics() const override;
    int ScaleLogical(int value) const override;
    int MeasureTextWidth(TextStyleId style, std::string_view text) const override;
    TextLayoutResult MeasureTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options) const override;
    void DrawText(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) const override;
    TextLayoutResult DrawTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) override;
    void PushClipRect(const RenderRect& rect) override;
    void PopClipRect() override;
    void PushTranslation(RenderPoint offset) override;
    void PopTranslation() override;
    bool DrawIcon(std::string_view iconName, const RenderRect& rect) override;
    bool FillSolidRect(const RenderRect& rect, RenderColorId color) override;
    bool FillSolidRoundedRect(const RenderRect& rect, int radius, RenderColorId color) override;
    bool FillSolidEllipse(const RenderRect& rect, RenderColorId color) override;
    bool FillSolidDiamond(const RenderRect& rect, RenderColorId color) override;
    bool DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke) override;
    bool DrawSolidRoundedRect(const RenderRect& rect, int radius, const RenderStroke& stroke) override;
    bool DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke) override;
    bool DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke) override;
    bool DrawArc(const RenderArc& arc, const RenderStroke& stroke) override;
    bool DrawArcs(std::span<const RenderArc> arcs, const RenderStroke& stroke) override;
    bool DrawPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke) override;
    bool FillPath(const RenderPath& path, RenderColorId color) override;
    bool FillPaths(std::span<const RenderPath> paths, RenderColorId color) override;

private:
    bool InitializeDirect2D();
    bool InitializeWic();
    void ShutdownDirect2D();
    bool LoadIcons();
    void ReleaseIcons();
    bool RebuildTextFormatsAndMetrics();
    bool EnsureWindowRenderTarget(int width, int height);
    bool BeginDirect2DDraw(ID2D1RenderTarget* target);
    void EndDirect2DDraw();
    bool BeginWindowDraw(int width, int height);
    void EndWindowDraw();
    bool SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath);
    ID2D1SolidColorBrush* D2DSolidBrush(RenderColorId color);
    IDWriteTextFormat* DWriteTextFormat(TextStyleId style) const;
    bool CreateDWriteTextFormats();
    void ConfigureDWriteTextFormat(IDWriteTextFormat* format, const TextLayoutOptions& options) const;
    TextLayoutResult MeasureTextBlockD2D(const RenderRect& rect,
        const std::wstring& wideText,
        TextStyleId style,
        const TextLayoutOptions& options,
        Microsoft::WRL::ComPtr<IDWriteTextLayout>* layout = nullptr) const;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DPathGeometry() const;
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> CreateD2DGeometryGroup(
        std::span<const Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometries, size_t count) const;
    bool FillD2DGeometry(ID2D1Geometry* geometry, RenderColorId color);
    bool DrawD2DGeometry(ID2D1Geometry* geometry, const RenderStroke& stroke);
    bool IsDrawActive() const;

    RendererStyle style_{};
    std::vector<std::pair<std::string, Microsoft::WRL::ComPtr<IWICBitmapSource>>> icons_;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, 9> dwriteTextFormats_{};
    TextStyleMetrics textStyleMetrics_{};
    RendererPalette palette_;
    D2DCache d2dCache_;
    RendererTextWidthCache textWidthCache_;
    std::string lastError_;
    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> d2dWindowRenderTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> d2dSolidStrokeStyle_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> d2dDashedStrokeStyle_;
    ID2D1RenderTarget* d2dActiveRenderTarget_ = nullptr;
    bool d2dImmediatePresent_ = false;
    bool wicComInitialized_ = false;
    int d2dClipDepth_ = 0;
    std::vector<D2D1_MATRIX_3X2_F> d2dTransformStack_;
};
