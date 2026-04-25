#include "renderer/impl/d2d_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <utility>

#include "renderer/impl/d2d_render_conversions.h"
#include "resource.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

std::size_t TextStyleSlot(TextStyleId style) {
    return static_cast<std::size_t>(style);
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

D2DRenderer::D2DRenderer(Trace& trace) : trace_(trace), palette_(style_.colors) {}

D2DRenderer::~D2DRenderer() {
    Shutdown();
}

bool D2DRenderer::SetStyle(const RendererStyle& style) {
    lastError_.clear();
    style_ = style;
    style_.scale = std::clamp(style_.scale, 0.1, 16.0);
    palette_.Rebuild(style_.colors);
    if (!InitializeDirect2D() || !LoadIcons() || !RebuildTextFormatsAndMetrics()) {
        return false;
    }
    return true;
}

void D2DRenderer::AttachWindow(HWND hwnd) {
    if (hwnd_ == hwnd) {
        return;
    }
    hwnd_ = hwnd;
    DiscardWindowTarget("window_attach");
}

void D2DRenderer::Shutdown() {
    dwriteTextFormats_ = {};
    textStyleMetrics_ = {};
    textWidthCache_.Clear();
    d2dTransformStack_.clear();
    d2dCache_.Clear();
    ReleaseIcons();
    ShutdownDirect2D();
}

void D2DRenderer::SetImmediatePresent(bool enabled) {
    if (d2dImmediatePresent_ == enabled) {
        return;
    }
    d2dImmediatePresent_ = enabled;
    DiscardWindowTarget("present_mode_change");
}

void D2DRenderer::SetTraceSuppressed(bool suppressed) {
    traceSuppressed_ = suppressed;
}

const std::string& D2DRenderer::LastError() const {
    return lastError_;
}

const TextStyleMetrics& D2DRenderer::TextMetrics() const {
    return textStyleMetrics_;
}

bool D2DRenderer::IsDrawActive() const {
    return d2dActiveRenderTarget_ != nullptr;
}

bool D2DRenderer::DrawWindow(int width, int height, const DrawCallback& draw) {
    if (!BeginWindowDraw(width, height)) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(palette_.Get(RenderColorId::Background).ToD2DColorF());
    draw();
    EndWindowDraw();
    return lastError_.empty();
}

bool D2DRenderer::DrawOffscreen(int width, int height, const DrawCallback& draw) {
    if (!InitializeDirect2D()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    const UINT bitmapWidth = static_cast<UINT>(std::max(1, width));
    const UINT bitmapHeight = static_cast<UINT>(std::max(1, height));
    HRESULT hr = wicFactory_->CreateBitmap(
        bitmapWidth, bitmapHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
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

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get())) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(palette_.Get(RenderColorId::Background).ToD2DColorF());
    draw();
    EndDirect2DDraw();
    return lastError_.empty();
}

bool D2DRenderer::SavePng(const std::filesystem::path& imagePath, int width, int height, const DrawCallback& draw) {
    if (!InitializeDirect2D()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    const UINT bitmapWidth = static_cast<UINT>(std::max(1, width));
    const UINT bitmapHeight = static_cast<UINT>(std::max(1, height));
    HRESULT hr = wicFactory_->CreateBitmap(
        bitmapWidth, bitmapHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
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

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get())) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(palette_.Get(RenderColorId::Background).ToD2DColorF());
    draw();
    EndDirect2DDraw();
    if (!lastError_.empty()) {
        return false;
    }
    return SaveWicBitmapPng(bitmap.Get(), imagePath);
}

TextLayoutResult D2DRenderer::MeasureTextBlock(
    const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const {
    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return TextLayoutResult{rect};
    }
    return MeasureTextBlockD2D(rect, wideText, style, options, nullptr);
}

TextLayoutResult D2DRenderer::DrawTextBlock(const RenderRect& rect,
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

void D2DRenderer::DrawText(const RenderRect& rect,
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
    ID2D1SolidColorBrush* brush = const_cast<D2DRenderer*>(this)->D2DSolidBrush(color);
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

int D2DRenderer::ScaleLogical(int value) const {
    if (value <= 0) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(value) * style_.scale)));
}

int D2DRenderer::MeasureTextWidth(TextStyleId style, std::string_view text) const {
    if (const std::optional<int> cachedWidth = textWidthCache_.Find(style, text)) {
        return *cachedWidth;
    }

    if (dwriteFactory_ == nullptr) {
        return 0;
    }

    const std::wstring wideText = WideFromUtf8(text);
    if (wideText.empty()) {
        return 0;
    }
    const RenderRect measureRect{0, 0, 4096, 4096};
    const int width = std::max(0,
        static_cast<int>(MeasureTextBlockD2D(measureRect,
            wideText,
            style,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
            nullptr)
                .textRect.right));
    const_cast<RendererTextWidthCache&>(textWidthCache_).Store(style, text, width);
    return width;
}

bool D2DRenderer::SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath) {
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

bool D2DRenderer::InitializeDirect2D() {
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

bool D2DRenderer::InitializeWic() {
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

void D2DRenderer::ShutdownDirect2D() {
    d2dClipDepth_ = 0;
    d2dActiveRenderTarget_ = nullptr;
    d2dCache_.ResetTarget();
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

bool D2DRenderer::EnsureWindowRenderTarget(int width, int height) {
    if (hwnd_ == nullptr || !InitializeDirect2D()) {
        return false;
    }

    const UINT targetWidth = static_cast<UINT>(std::max(1, width));
    const UINT targetHeight = static_cast<UINT>(std::max(1, height));
    if (d2dWindowRenderTarget_ == nullptr) {
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const D2D1_PRESENT_OPTIONS presentOptions =
            d2dImmediatePresent_ ? D2D1_PRESENT_OPTIONS_IMMEDIATELY : D2D1_PRESENT_OPTIONS_NONE;
        const HRESULT hr = d2dFactory_->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(targetWidth, targetHeight), presentOptions),
            d2dWindowRenderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || d2dWindowRenderTarget_ == nullptr) {
            lastError_ = "renderer:d2d_hwnd_target_failed hr=" + FormatHresult(hr);
            return false;
        }
        d2dCache_.ResetTarget();
        return true;
    }

    const D2D1_SIZE_U currentSize = d2dWindowRenderTarget_->GetPixelSize();
    if (currentSize.width == targetWidth && currentSize.height == targetHeight) {
        return true;
    }
    const HRESULT hr = d2dWindowRenderTarget_->Resize(D2D1::SizeU(targetWidth, targetHeight));
    if (FAILED(hr)) {
        d2dWindowRenderTarget_.Reset();
        d2dCache_.ResetTarget();
        return EnsureWindowRenderTarget(width, height);
    }
    d2dCache_.Clear();
    return true;
}

bool D2DRenderer::BeginDirect2DDraw(ID2D1RenderTarget* target) {
    if (target == nullptr) {
        lastError_ = "renderer:d2d_target_missing";
        return false;
    }
    d2dCache_.AttachTarget(target);
    lastError_.clear();
    d2dActiveRenderTarget_ = target;
    d2dActiveRenderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2dActiveRenderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    d2dActiveRenderTarget_->BeginDraw();
    d2dClipDepth_ = 0;
    d2dTransformStack_.clear();
    return true;
}

void D2DRenderer::EndDirect2DDraw() {
    if (d2dActiveRenderTarget_ == nullptr) {
        return;
    }
    while (d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
    while (!d2dTransformStack_.empty()) {
        PopTranslation();
    }
    const bool activeWindowTarget = d2dActiveRenderTarget_ == d2dWindowRenderTarget_.Get();
    const HRESULT hr = d2dActiveRenderTarget_->EndDraw();
    d2dActiveRenderTarget_ = nullptr;
    if (!activeWindowTarget) {
        d2dCache_.ResetTarget();
    }
    if (activeWindowTarget && hr == D2DERR_RECREATE_TARGET) {
        DiscardWindowTarget("recreate_target");
    } else if (FAILED(hr)) {
        if (activeWindowTarget) {
            DiscardWindowTarget("end_draw_failed");
        }
        lastError_ = "renderer:d2d_end_draw_failed hr=" + FormatHresult(hr);
    }
}

bool D2DRenderer::BeginWindowDraw(int width, int height) {
    if (!EnsureWindowRenderTarget(width, height) || d2dWindowRenderTarget_ == nullptr) {
        return false;
    }
    return BeginDirect2DDraw(d2dWindowRenderTarget_.Get());
}

void D2DRenderer::EndWindowDraw() {
    EndDirect2DDraw();
}

void D2DRenderer::DiscardWindowTarget(std::string_view reason) {
    if (!reason.empty()) {
        WriteTrace("renderer:d2d_window_target_discard reason=\"" + std::string(reason) + "\"");
    }
    d2dClipDepth_ = 0;
    if (d2dActiveRenderTarget_ == d2dWindowRenderTarget_.Get()) {
        d2dActiveRenderTarget_ = nullptr;
    }
    d2dWindowRenderTarget_.Reset();
    d2dCache_.ResetTarget();
}

ID2D1SolidColorBrush* D2DRenderer::D2DSolidBrush(RenderColorId colorId) {
    if (d2dActiveRenderTarget_ == nullptr) {
        return nullptr;
    }
    return d2dCache_.SolidBrush(d2dActiveRenderTarget_, palette_.Get(colorId));
}

void D2DRenderer::PushClipRect(const RenderRect& rect) {
    if (IsDrawActive()) {
        d2dActiveRenderTarget_->PushAxisAlignedClip(D2DRectFromRenderRect(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++d2dClipDepth_;
    }
}

void D2DRenderer::PopClipRect() {
    if (IsDrawActive() && d2dClipDepth_ > 0) {
        d2dActiveRenderTarget_->PopAxisAlignedClip();
        --d2dClipDepth_;
    }
}

void D2DRenderer::PushTranslation(RenderPoint offset) {
    if (!IsDrawActive()) {
        return;
    }
    D2D1_MATRIX_3X2_F previousTransform{};
    d2dActiveRenderTarget_->GetTransform(&previousTransform);
    d2dTransformStack_.push_back(previousTransform);
    const D2D1_MATRIX_3X2_F translation =
        D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y));
    d2dActiveRenderTarget_->SetTransform(translation * previousTransform);
}

void D2DRenderer::PopTranslation() {
    if (!IsDrawActive() || d2dTransformStack_.empty()) {
        return;
    }
    d2dActiveRenderTarget_->SetTransform(d2dTransformStack_.back());
    d2dTransformStack_.pop_back();
}

bool D2DRenderer::DrawIcon(std::string_view iconName, const RenderRect& rect) {
    if (!IsDrawActive() || iconName.empty() || rect.IsEmpty()) {
        return false;
    }
    d2dCache_.DrawIcon(wicFactory_.Get(), d2dActiveRenderTarget_, icons_, iconName, rect);
    return true;
}

bool D2DRenderer::FillSolidRect(const RenderRect& rect, RenderColorId color) {
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

bool D2DRenderer::FillSolidRoundedRect(const RenderRect& rect, int radius, RenderColorId color) {
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

bool D2DRenderer::FillSolidEllipse(const RenderRect& rect, RenderColorId color) {
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

bool D2DRenderer::FillSolidDiamond(const RenderRect& rect, RenderColorId color) {
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

bool D2DRenderer::DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke) {
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

bool D2DRenderer::DrawSolidRoundedRect(const RenderRect& rect, int radius, const RenderStroke& stroke) {
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

bool D2DRenderer::DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke) {
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

bool D2DRenderer::DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke) {
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

bool D2DRenderer::DrawArc(const RenderArc& arc, const RenderStroke& stroke) {
    if (!IsDrawActive()) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderArcGeometry(d2dFactory_.Get(), arc);
    return DrawD2DGeometry(geometry.Get(), stroke);
}

bool D2DRenderer::DrawArcs(std::span<const RenderArc> arcs, const RenderStroke& stroke) {
    if (!IsDrawActive() || arcs.empty()) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderArcsGeometry(d2dFactory_.Get(), arcs);
    return DrawD2DGeometry(geometry.Get(), stroke);
}

Microsoft::WRL::ComPtr<ID2D1PathGeometry> D2DRenderer::CreateD2DPathGeometry() const {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (d2dFactory_ != nullptr) {
        d2dFactory_->CreatePathGeometry(geometry.GetAddressOf());
    }
    return geometry;
}

Microsoft::WRL::ComPtr<ID2D1GeometryGroup> D2DRenderer::CreateD2DGeometryGroup(
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

bool D2DRenderer::FillD2DGeometry(ID2D1Geometry* geometry, RenderColorId color) {
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

bool D2DRenderer::DrawD2DGeometry(ID2D1Geometry* geometry, const RenderStroke& stroke) {
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

bool D2DRenderer::FillPath(const RenderPath& path, RenderColorId color) {
    if (!IsDrawActive() || path.IsEmpty()) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry = CreateD2DRenderPathGeometry(d2dFactory_.Get(), path);
    return FillD2DGeometry(geometry.Get(), color);
}

bool D2DRenderer::FillPaths(std::span<const RenderPath> paths, RenderColorId color) {
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

bool D2DRenderer::DrawPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke) {
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

IDWriteTextFormat* D2DRenderer::DWriteTextFormat(TextStyleId style) const {
    return dwriteTextFormats_[TextStyleSlot(style)].Get();
}

bool D2DRenderer::CreateDWriteTextFormats() {
    dwriteTextFormats_ = {};
    if (dwriteFactory_ == nullptr) {
        return true;
    }

    const auto createFormat = [&](TextStyleId style) {
        UiFontConfig fontConfig = FontConfigForStyle(style_.fonts, style);
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

void D2DRenderer::ConfigureDWriteTextFormat(IDWriteTextFormat* format, const TextLayoutOptions& options) const {
    if (format == nullptr) {
        return;
    }
    format->SetTextAlignment(DWriteTextAlignment(options));
    format->SetParagraphAlignment(DWriteParagraphAlignment(options));
    format->SetWordWrapping(options.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
}

TextLayoutResult D2DRenderer::MeasureTextBlockD2D(const RenderRect& rect,
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

bool D2DRenderer::LoadIcons() {
    ReleaseIcons();
    if (wicFactory_ == nullptr && !InitializeWic()) {
        return false;
    }
    std::set<std::string> uniqueIcons(style_.iconNames.begin(), style_.iconNames.end());
    for (const auto& iconName : uniqueIcons) {
        if (iconName.empty()) {
            continue;
        }
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
            TintMonochromeBitmapSource(wicFactory_.Get(), bitmap.Get(), palette_.Get(RenderColorId::Icon));
        if (tintedBitmap == nullptr) {
            lastError_ = "renderer:icon_tint_failed name=\"" + iconName + "\" resource=" + std::to_string(resourceId);
            ReleaseIcons();
            return false;
        }
        icons_.push_back({iconName, std::move(tintedBitmap)});
    }
    return true;
}

void D2DRenderer::ReleaseIcons() {
    d2dCache_.ClearIconBitmaps();
    icons_.clear();
}

void D2DRenderer::WriteTrace(const std::string& text) const {
    if (traceSuppressed_ && text.rfind("renderer:", 0) == 0) {
        return;
    }
    trace_.Write(text);
}

bool D2DRenderer::RebuildTextFormatsAndMetrics() {
    if (!CreateDWriteTextFormats()) {
        lastError_ = "renderer:text_format_create_failed";
        return false;
    }
    textWidthCache_.Clear();
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
    return true;
}
