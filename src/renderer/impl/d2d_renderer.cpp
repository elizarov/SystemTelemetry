#include "renderer/impl/d2d_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <memory>
#include <utility>

#include "renderer/impl/d2d_render_conversions.h"
#include "renderer/png_export.h"
#include "resource.h"
#include "util/lightweight_mutex.h"
#include "util/resource_strings.h"
#include "util/text_encoding.h"
#include "util/text_format.h"
#include "util/win32_format.h"

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

constexpr int kPanelIconAtlasCellSize = 64;
constexpr UINT kDxgiSwapChainBufferCount = 2;
constexpr char kLocaleName[] = "en-us";
constexpr char kPngResourceType[] = "PNG";
constexpr wchar_t kTextMeasureSample[] = L"Ag";  // DWrite text layout measures UTF-16 sample text.

const void* D2DRenderBitmapResourceTypeToken() {
    static const int token = 0;
    return &token;
}

void SetHresultError(std::string& errorText, ResourceStringId prefix, HRESULT hr) {
    AssignFormat(errorText, RES_STR("%s hr="), ResourceStringText(prefix));
    AppendHresult(errorText, hr);
}

void SetPrefixedHresultError(std::string& errorText, std::string_view prefix, ResourceStringId suffix, HRESULT hr) {
    AssignFormat(
        errorText, RES_STR("%.*s%s hr="), static_cast<int>(prefix.size()), prefix.data(), ResourceStringText(suffix));
    AppendHresult(errorText, hr);
}

class D2DSharedDevice final {
public:
    bool Ensure(std::string& errorText) {
        const LightweightMutexLock lock(mutex_);
        if (d2dFactory_ != nullptr && d2dDevice_ != nullptr && d3dDevice_ != nullptr && dxgiFactory_ != nullptr) {
            return true;
        }

        if (d2dFactory_ == nullptr) {
            D2D1_FACTORY_OPTIONS options{};
            const HRESULT hr = D2D1CreateFactory(
                D2D1_FACTORY_TYPE_MULTI_THREADED,
                __uuidof(ID2D1Factory1),
                &options,
                reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()));
            if (FAILED(hr) || d2dFactory_ == nullptr) {
                SetHresultError(errorText, RES_STR("d2d_factory_failed"), hr);
                return false;
            }
        }

        if (d3dDevice_ == nullptr) {
            const std::array<D3D_FEATURE_LEVEL, 4> preferredFeatureLevels{
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };
            const std::array<D3D_FEATURE_LEVEL, 3> fallbackFeatureLevels{
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };
            D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_10_0;
            const auto createDevice = [&](D3D_DRIVER_TYPE driverType, auto featureLevels) {
                return D3D11CreateDevice(
                    nullptr,
                    driverType,
                    nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    featureLevels.data(),
                    static_cast<UINT>(featureLevels.size()),
                    D3D11_SDK_VERSION,
                    d3dDevice_.ReleaseAndGetAddressOf(),
                    &createdFeatureLevel,
                    d3dContext_.ReleaseAndGetAddressOf());
            };
            HRESULT hr = createDevice(D3D_DRIVER_TYPE_HARDWARE, preferredFeatureLevels);
            if (hr == E_INVALIDARG) {
                hr = createDevice(D3D_DRIVER_TYPE_HARDWARE, fallbackFeatureLevels);
            }
            if (FAILED(hr) || d3dDevice_ == nullptr) {
                hr = createDevice(D3D_DRIVER_TYPE_WARP, preferredFeatureLevels);
                if (hr == E_INVALIDARG) {
                    hr = createDevice(D3D_DRIVER_TYPE_WARP, fallbackFeatureLevels);
                }
            }
            if (FAILED(hr) || d3dDevice_ == nullptr) {
                SetHresultError(errorText, RES_STR("d3d_device_failed"), hr);
                return false;
            }
        }

        if (d2dDevice_ == nullptr || dxgiFactory_ == nullptr) {
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
            HRESULT hr = d3dDevice_.As(&dxgiDevice);
            if (FAILED(hr) || dxgiDevice == nullptr) {
                SetHresultError(errorText, RES_STR("dxgi_device_query_failed"), hr);
                return false;
            }
            hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.ReleaseAndGetAddressOf());
            if (FAILED(hr) || d2dDevice_ == nullptr) {
                SetHresultError(errorText, RES_STR("d2d_device_failed"), hr);
                return false;
            }

            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
            if (FAILED(hr) || adapter == nullptr) {
                SetHresultError(errorText, RES_STR("dxgi_adapter_failed"), hr);
                return false;
            }
            hr = adapter->GetParent(IID_PPV_ARGS(dxgiFactory_.ReleaseAndGetAddressOf()));
            if (FAILED(hr) || dxgiFactory_ == nullptr) {
                SetHresultError(errorText, RES_STR("dxgi_factory_failed"), hr);
                return false;
            }
        }
        return true;
    }

    ID2D1Factory1* D2DFactory() const {
        return d2dFactory_.Get();
    }

    ID2D1Device* D2DDevice() const {
        return d2dDevice_.Get();
    }

    ID3D11Device* D3DDevice() const {
        return d3dDevice_.Get();
    }

    IDXGIFactory2* DxgiFactory() const {
        return dxgiFactory_.Get();
    }

private:
    LightweightMutex mutex_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
};

D2DSharedDevice& SharedD2DDevice() {
    static D2DSharedDevice device;
    return device;
}

class D2DRenderBitmapResource final : public RenderBitmapResource {
public:
    D2DRenderBitmapResource(Microsoft::WRL::ComPtr<IWICBitmap> bitmap, Microsoft::WRL::ComPtr<ID2D1RenderTarget> target)
        : wicBitmap_(std::move(bitmap)), wicRenderTarget_(std::move(target)) {}

    explicit D2DRenderBitmapResource(Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap) : targetBitmap_(std::move(bitmap)) {}

    const void* TypeToken() const override {
        return D2DRenderBitmapResourceTypeToken();
    }

    IWICBitmap* WicBitmap() const {
        return wicBitmap_.Get();
    }

    ID2D1RenderTarget* WicRenderTarget() const {
        return wicRenderTarget_.Get();
    }

    ID2D1Bitmap* D2DBitmap() const {
        return targetBitmap_.Get();
    }

    ID2D1Bitmap1* TargetBitmap() const {
        return targetBitmap_.Get();
    }

    ID2D1Bitmap* CachedD2DBitmap(ID2D1RenderTarget* target) const {
        return cachedD2DBitmapTarget_ == target ? cachedD2DBitmap_.Get() : nullptr;
    }

    void CacheD2DBitmap(ID2D1RenderTarget* target, Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap) const {
        cachedD2DBitmapTarget_ = target;
        cachedD2DBitmap_ = std::move(bitmap);
    }

    ID2D1BitmapBrush* CachedD2DBitmapBrush(ID2D1RenderTarget* target) const {
        return cachedD2DBitmapBrushTarget_ == target ? cachedD2DBitmapBrush_.Get() : nullptr;
    }

    void CacheD2DBitmapBrush(ID2D1RenderTarget* target, Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush) const {
        cachedD2DBitmapBrushTarget_ = target;
        cachedD2DBitmapBrush_ = std::move(brush);
    }

    void InvalidateCachedD2DBitmap() const {
        cachedD2DBitmapTarget_ = nullptr;
        cachedD2DBitmap_.Reset();
        cachedD2DBitmapBrushTarget_ = nullptr;
        cachedD2DBitmapBrush_.Reset();
    }

private:
    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> wicRenderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    mutable ID2D1RenderTarget* cachedD2DBitmapTarget_ = nullptr;
    mutable Microsoft::WRL::ComPtr<ID2D1Bitmap> cachedD2DBitmap_;
    mutable ID2D1RenderTarget* cachedD2DBitmapBrushTarget_ = nullptr;
    mutable Microsoft::WRL::ComPtr<ID2D1BitmapBrush> cachedD2DBitmapBrush_;
};

int GetPanelIconAtlasSlot(std::string_view iconName) {
    if (iconName == "cpu")
        return 0;
    if (iconName == "gpu")
        return 1;
    if (iconName == "network")
        return 2;
    if (iconName == "storage")
        return 3;
    if (iconName == "time")
        return 4;
    return -1;
}

UiFontConfig FontConfigForStyle(const FontsConfig& fonts, TextStyleId style) {
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

Microsoft::WRL::ComPtr<IWICBitmapSource> LoadPngResourceMask(IWICImagingFactory* wicFactory, UINT resourceId) {
    Microsoft::WRL::ComPtr<IWICBitmapSource> bitmapSource;
    if (wicFactory == nullptr) {
        return bitmapSource;
    }

    HMODULE module = GetModuleHandleA(nullptr);
    if (module == nullptr) {
        return bitmapSource;
    }

    HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(resourceId), kPngResourceType);
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
        frame.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return bitmapSource;
    }

    bitmapSource = converter;
    return bitmapSource;
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> CreatePanelIconAtlasMaskBitmap(
    ID2D1RenderTarget* target, IWICBitmapSource* source) {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    if (target == nullptr || source == nullptr) {
        return bitmap;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(source->GetSize(&width, &height)) || width == 0 || height == 0) {
        return bitmap;
    }

    const UINT maskStride = width;
    std::vector<BYTE> mask(static_cast<size_t>(maskStride) * static_cast<size_t>(height));
    if (FAILED(source->CopyPixels(nullptr, maskStride, static_cast<UINT>(mask.size()), mask.data()))) {
        return bitmap;
    }

    const D2D1_BITMAP_PROPERTIES properties =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(target->CreateBitmap(
            D2D1::SizeU(width, height), mask.data(), maskStride, properties, bitmap.GetAddressOf()))) {
        return {};
    }

    return bitmap;
}

}  // namespace

D2DRenderer::D2DRenderer() : palette_(style_.colors, style_.layoutGuideSheet) {}

D2DRenderer::~D2DRenderer() {
    Shutdown();
}

bool D2DRenderer::SetStyle(const RendererStyle& style) {
    lastError_.clear();
    RendererStyle nextStyle = style;
    nextStyle.scale = std::clamp(nextStyle.scale, 0.1, 16.0);

    const bool initialized = d2dFactory_ != nullptr && dwriteFactory_ != nullptr;
    const bool colorsChanged =
        style_.colors != nextStyle.colors || style_.layoutGuideSheet != nextStyle.layoutGuideSheet;
    const bool iconSourcesChanged = !initialized || style_.iconNames != nextStyle.iconNames;
    const bool textFormatsChanged =
        !initialized || style_.fonts != nextStyle.fonts || std::abs(style_.scale - nextStyle.scale) >= 0.0001;

    style_ = std::move(nextStyle);
    if (!InitializeDirect2D()) {
        return false;
    }
    if (colorsChanged || !initialized) {
        palette_.Rebuild(style_.colors, style_.layoutGuideSheet);
        d2dCache_.Clear();
    }
    if (iconSourcesChanged && !LoadIcons()) {
        return false;
    }
    if (textFormatsChanged && !RebuildTextFormatsAndMetrics()) {
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
    if (!BeginWindowDraw(width, height, false)) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(palette_.Get(RenderColorId::Background).ToD2DColorF());
    draw();
    EndWindowDraw();
    if (lastError_.empty() && dxgiSwapChain_ != nullptr) {
        dxgiRetainedBuffersPrimed_ = 0;
    }
    return lastError_.empty();
}

bool D2DRenderer::DrawWindowRetained(int width, int height, const DrawCallback& draw) {
    if (!BeginWindowDraw(width, height, true)) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(palette_.Get(RenderColorId::Background).ToD2DColorF());
    draw();
    EndWindowDraw();
    if (lastError_.empty() && dxgiSwapChain_ != nullptr) {
        dxgiRetainedBuffersPrimed_ = 1;
    }
    return lastError_.empty();
}

bool D2DRenderer::DrawWindowDirty(
    int width, int height, std::span<const RenderRect> dirtyRects, const DirtyDrawCallback& draw) {
    if (dirtyRects.empty()) {
        return true;
    }
    if (!BeginWindowDraw(width, height, true)) {
        return false;
    }
    const RenderRect fullSurface{0, 0, std::max(1, width), std::max(1, height)};
    // Flip chains retain each physical back buffer separately; prime all buffers before dirty-only redraws.
    const bool primeDxgiRetainedBuffer =
        dxgiSwapChain_ != nullptr && dxgiRetainedBuffersPrimed_ < kDxgiSwapChainBufferCount;
    const std::span<const RenderRect> redrawRects =
        primeDxgiRetainedBuffer ? std::span<const RenderRect>(&fullSurface, 1) : dirtyRects;
    draw(redrawRects);
    EndWindowDraw();
    if (lastError_.empty() && primeDxgiRetainedBuffer && dxgiSwapChain_ != nullptr) {
        ++dxgiRetainedBuffersPrimed_;
    }
    return lastError_.empty();
}

bool D2DRenderer::DrawOffscreen(int width, int height, const DrawCallback& draw) {
    return DrawToWicBitmap(width, height, draw, "offscreen");
}

bool D2DRenderer::DrawToBitmap(
    RenderBitmap& output, int width, int height, RenderBitmapClear clear, const DrawCallback& draw) {
    if (!InitializeDirect2D()) {
        return false;
    }

    const UINT bitmapWidth = static_cast<UINT>(std::max(1, width));
    const UINT bitmapHeight = static_cast<UINT>(std::max(1, height));
    const D2D1_COLOR_F clearColor = clear == RenderBitmapClear::Transparent
        ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)
        : palette_.Get(RenderColorId::Background).ToD2DColorF();
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> bitmapRenderTarget;
    if (output.width == static_cast<int>(bitmapWidth) && output.height == static_cast<int>(bitmapHeight) &&
        output.storage == RenderBitmapStorage::Generic && output.resource != nullptr &&
        output.resource->TypeToken() == D2DRenderBitmapResourceTypeToken()) {
        const auto* resource = static_cast<const D2DRenderBitmapResource*>(output.resource.get());
        if (resource->WicBitmap() != nullptr && resource->WicRenderTarget() != nullptr) {
            bitmap = resource->WicBitmap();
            bitmapRenderTarget = resource->WicRenderTarget();
            resource->InvalidateCachedD2DBitmap();
        }
    }
    HRESULT hr = S_OK;
    if (bitmap == nullptr || bitmapRenderTarget == nullptr) {
        bitmap.Reset();
        bitmapRenderTarget.Reset();
        hr = wicFactory_->CreateBitmap(
            bitmapWidth, bitmapHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
        if (FAILED(hr) || bitmap == nullptr) {
            SetHresultError(lastError_, RES_STR("layer_wic_bitmap_failed"), hr);
            return false;
        }

        hr = d2dFactory_->CreateWicBitmapRenderTarget(
            bitmap.Get(),
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96.0f,
                96.0f),
            bitmapRenderTarget.GetAddressOf());
        if (FAILED(hr) || bitmapRenderTarget == nullptr) {
            SetHresultError(lastError_, RES_STR("layer_d2d_target_failed"), hr);
            return false;
        }
    }

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get(), ActiveDrawTarget::Bitmap)) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(clearColor);
    draw();
    EndDirect2DDraw();
    if (!lastError_.empty()) {
        return false;
    }

    output.width = static_cast<int>(bitmapWidth);
    output.height = static_cast<int>(bitmapHeight);
    output.storage = RenderBitmapStorage::Generic;
    output.resource = std::make_shared<D2DRenderBitmapResource>(std::move(bitmap), std::move(bitmapRenderTarget));
    return true;
}

bool D2DRenderer::DrawToLiveLayerBitmap(
    RenderBitmap& output, int width, int height, RenderBitmapClear clear, const DrawCallback& draw) {
    if (!EnsureDeviceContext()) {
        return false;
    }

    const UINT bitmapWidth = static_cast<UINT>(std::max(1, width));
    const UINT bitmapHeight = static_cast<UINT>(std::max(1, height));
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap;
    if (output.width == static_cast<int>(bitmapWidth) && output.height == static_cast<int>(bitmapHeight) &&
        output.storage == RenderBitmapStorage::LiveLayer && output.resource != nullptr &&
        output.resource->TypeToken() == D2DRenderBitmapResourceTypeToken()) {
        const auto* resource = static_cast<const D2DRenderBitmapResource*>(output.resource.get());
        targetBitmap = resource->TargetBitmap();
    }
    if (targetBitmap == nullptr) {
        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        const HRESULT hr = d2dDeviceContext_->CreateBitmap(
            D2D1::SizeU(bitmapWidth, bitmapHeight), nullptr, 0, properties, targetBitmap.GetAddressOf());
        if (FAILED(hr) || targetBitmap == nullptr) {
            SetHresultError(lastError_, RES_STR("layer_d2d_bitmap_failed"), hr);
            return false;
        }
    }

    Microsoft::WRL::ComPtr<ID2D1Image> previousTarget;
    d2dDeviceContext_->GetTarget(previousTarget.GetAddressOf());
    d2dDeviceContext_->SetTarget(targetBitmap.Get());
    if (!BeginDirect2DDraw(d2dDeviceContext_.Get(), ActiveDrawTarget::Bitmap)) {
        d2dDeviceContext_->SetTarget(previousTarget.Get());
        return false;
    }
    const D2D1_COLOR_F clearColor = clear == RenderBitmapClear::Transparent
        ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)
        : palette_.Get(RenderColorId::Background).ToD2DColorF();
    d2dActiveRenderTarget_->Clear(clearColor);
    draw();
    EndDirect2DDraw();
    d2dDeviceContext_->SetTarget(previousTarget.Get());
    if (!lastError_.empty()) {
        return false;
    }

    output.width = static_cast<int>(bitmapWidth);
    output.height = static_cast<int>(bitmapHeight);
    output.storage = RenderBitmapStorage::LiveLayer;
    output.resource = std::make_shared<D2DRenderBitmapResource>(std::move(targetBitmap));
    return true;
}

bool D2DRenderer::DrawToWicBitmap(
    int width,
    int height,
    const DrawCallback& draw,
    std::string_view errorPrefix,
    Microsoft::WRL::ComPtr<IWICBitmap>* renderedBitmap) {
    if (!InitializeDirect2D()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    const UINT bitmapWidth = static_cast<UINT>(std::max(1, width));
    const UINT bitmapHeight = static_cast<UINT>(std::max(1, height));
    HRESULT hr = wicFactory_->CreateBitmap(
        bitmapWidth, bitmapHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr) || bitmap == nullptr) {
        SetPrefixedHresultError(lastError_, errorPrefix, RES_STR("_wic_bitmap_failed"), hr);
        return false;
    }

    Microsoft::WRL::ComPtr<ID2D1RenderTarget> bitmapRenderTarget;
    hr = d2dFactory_->CreateWicBitmapRenderTarget(
        bitmap.Get(),
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f),
        bitmapRenderTarget.GetAddressOf());
    if (FAILED(hr) || bitmapRenderTarget == nullptr) {
        SetPrefixedHresultError(lastError_, errorPrefix, RES_STR("_d2d_target_failed"), hr);
        return false;
    }

    if (!BeginDirect2DDraw(bitmapRenderTarget.Get(), ActiveDrawTarget::Bitmap)) {
        return false;
    }
    d2dActiveRenderTarget_->Clear(palette_.Get(RenderColorId::Background).ToD2DColorF());
    draw();
    EndDirect2DDraw();
    if (!lastError_.empty()) {
        return false;
    }
    if (renderedBitmap != nullptr) {
        *renderedBitmap = bitmap;
    }
    return lastError_.empty();
}

bool D2DRenderer::SavePng(const FilePath& imagePath, int width, int height, const DrawCallback& draw) {
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    if (!DrawToWicBitmap(width, height, draw, "screenshot", &bitmap)) {
        return false;
    }
    return SaveWicBitmapPng(bitmap.Get(), imagePath);
}

TextLayoutResult D2DRenderer::MeasureTextBlock(
    const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const {
    const std::wstring wideText = WideFromText(text);
    if (wideText.empty()) {
        return TextLayoutResult{rect};
    }
    return MeasureTextBlockD2D(rect, wideText, style, options, nullptr);
}

TextLayoutResult D2DRenderer::DrawTextBlock(
    const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    RenderColorId color,
    const TextLayoutOptions& options) {
    TextLayoutResult result{rect};
    const std::wstring wideText = WideFromText(text);
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
    d2dActiveRenderTarget_->DrawText(
        wideText.c_str(),
        static_cast<UINT32>(wideText.size()),
        textFormat,
        D2DRectFromRenderRect(rect),
        brush,
        drawOptions);
    return result;
}

void D2DRenderer::DrawText(
    const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    RenderColorId color,
    const TextLayoutOptions& options) const {
    const std::wstring wideText = WideFromText(text);
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
    d2dActiveRenderTarget_->DrawText(
        wideText.c_str(),
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

    const std::wstring wideText = WideFromText(text);
    if (wideText.empty()) {
        return 0;
    }
    const RenderRect measureRect{0, 0, 4096, 4096};
    const int width = std::max(
        0,
        static_cast<int>(MeasureTextBlockD2D(
                             measureRect,
                             wideText,
                             style,
                             TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
                             nullptr)
                             .textRect.right));
    const_cast<RendererTextWidthCache&>(textWidthCache_).Store(style, text, width);
    return width;
}

bool D2DRenderer::SaveWicBitmapPng(IWICBitmap* bitmap, const FilePath& imagePath) {
    if (wicFactory_ == nullptr || bitmap == nullptr) {
        lastError_ = ResourceStringText(RES_STR("screenshot_wic_unavailable"));
        return false;
    }

    std::string errorText;
    if (!SaveWicBitmapSourcePng(
            wicFactory_.Get(), bitmap, imagePath, PngPixelFormat::BgrOpaque, "screenshot", &errorText)) {
        lastError_ = std::move(errorText);
        return false;
    }
    return true;
}

bool D2DRenderer::InitializeDirect2D() {
    if (d2dFactory_ == nullptr) {
        if (!SharedD2DDevice().Ensure(lastError_)) {
            return false;
        }
        d2dFactory_ = SharedD2DDevice().D2DFactory();
    }
    if (dwriteFactory_ == nullptr) {
        const HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || dwriteFactory_ == nullptr) {
            SetHresultError(lastError_, RES_STR("dwrite_factory_failed"), hr);
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
        const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
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

bool D2DRenderer::EnsureDeviceContext() {
    if (!InitializeDirect2D()) {
        return false;
    }
    if (d2dDeviceContext_ != nullptr) {
        return true;
    }
    ID2D1Device* device = SharedD2DDevice().D2DDevice();
    if (device == nullptr) {
        lastError_ = ResourceStringText(RES_STR("d2d_device_missing"));
        return false;
    }
    const HRESULT hr =
        device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dDeviceContext_.ReleaseAndGetAddressOf());
    if (FAILED(hr) || d2dDeviceContext_ == nullptr) {
        SetHresultError(lastError_, RES_STR("d2d_device_context_failed"), hr);
        return false;
    }
    return true;
}

bool D2DRenderer::InitializeWic() {
    if (wicFactory_ != nullptr) {
        return true;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        SetHresultError(lastError_, RES_STR("wic_com_init_failed"), initHr);
        return false;
    }
    wicComInitialized_ = initHr == S_OK || initHr == S_FALSE;

    const HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || wicFactory_ == nullptr) {
        SetHresultError(lastError_, RES_STR("wic_factory_failed"), hr);
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
    d2dActiveDrawTarget_ = ActiveDrawTarget::None;
    d2dCache_.ResetTarget();
    panelIconAtlasMask_.Reset();
    panelIconAtlasMaskTarget_ = nullptr;
    d2dDashedStrokeStyle_.Reset();
    d2dSolidStrokeStyle_.Reset();
    if (d2dDeviceContext_ != nullptr) {
        d2dDeviceContext_->SetTarget(nullptr);
    }
    dxgiWindowTargetBitmap_.Reset();
    dxgiSwapChain_.Reset();
    d2dDeviceContext_.Reset();
    d2dWindowRenderTarget_.Reset();
    d2dWindowRetainContents_ = false;
    dxgiWindowRetainContents_ = false;
    dxgiWindowWidth_ = 0;
    dxgiWindowHeight_ = 0;
    wicFactory_.Reset();
    if (wicComInitialized_) {
        CoUninitialize();
        wicComInitialized_ = false;
    }
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
}

bool D2DRenderer::EnsureWindowRenderTarget(int width, int height, bool retainContents) {
    if (hwnd_ == nullptr || !InitializeDirect2D()) {
        return false;
    }

    const UINT targetWidth = static_cast<UINT>(std::max(1, width));
    const UINT targetHeight = static_cast<UINT>(std::max(1, height));
    if (d2dWindowRenderTarget_ != nullptr && d2dWindowRetainContents_ != retainContents) {
        DiscardWindowTarget("retain_mode_change");
    }
    if (d2dWindowRenderTarget_ == nullptr) {
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const D2D1_PRESENT_OPTIONS presentOptions = static_cast<D2D1_PRESENT_OPTIONS>(
            (retainContents ? D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS : D2D1_PRESENT_OPTIONS_NONE) |
            (d2dImmediatePresent_ ? D2D1_PRESENT_OPTIONS_IMMEDIATELY : D2D1_PRESENT_OPTIONS_NONE));
        const HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
            properties,
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(targetWidth, targetHeight), presentOptions),
            d2dWindowRenderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || d2dWindowRenderTarget_ == nullptr) {
            SetHresultError(lastError_, RES_STR("d2d_hwnd_target_failed"), hr);
            return false;
        }
        d2dWindowRetainContents_ = retainContents;
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
        return EnsureWindowRenderTarget(width, height, retainContents);
    }
    d2dCache_.Clear();
    return true;
}

bool D2DRenderer::EnsureDxgiWindowTarget(int width, int height, bool retainContents) {
    if (hwnd_ == nullptr || !EnsureDeviceContext()) {
        return false;
    }

    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    if (dxgiSwapChain_ == nullptr) {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        swapChainDesc.Width = static_cast<UINT>(targetWidth);
        swapChainDesc.Height = static_cast<UINT>(targetHeight);
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = kDxgiSwapChainBufferCount;
        // Surface changes resize the HWND before the replacement back buffer exists; never stretch stale content.
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        IDXGIFactory2* factory = SharedD2DDevice().DxgiFactory();
        ID3D11Device* device = SharedD2DDevice().D3DDevice();
        if (factory == nullptr || device == nullptr) {
            lastError_ = ResourceStringText(RES_STR("dxgi_shared_device_missing"));
            return false;
        }
        const HRESULT hr = factory->CreateSwapChainForHwnd(
            device, hwnd_, &swapChainDesc, nullptr, nullptr, dxgiSwapChain_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || dxgiSwapChain_ == nullptr) {
            SetHresultError(lastError_, RES_STR("dxgi_swap_chain_failed"), hr);
            return false;
        }
        factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        dxgiWindowWidth_ = targetWidth;
        dxgiWindowHeight_ = targetHeight;
        dxgiWindowRetainContents_ = retainContents;
        dxgiRetainedBuffersPrimed_ = 0;
        return CreateDxgiWindowTargetBitmap();
    }

    if (dxgiWindowWidth_ == targetWidth && dxgiWindowHeight_ == targetHeight && dxgiWindowTargetBitmap_ != nullptr) {
        dxgiWindowRetainContents_ = retainContents;
        if (!retainContents) {
            dxgiRetainedBuffersPrimed_ = 0;
        }
        return true;
    }

    dxgiWindowTargetBitmap_.Reset();
    d2dDeviceContext_->SetTarget(nullptr);
    const HRESULT hr = dxgiSwapChain_->ResizeBuffers(
        0, static_cast<UINT>(targetWidth), static_cast<UINT>(targetHeight), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        DiscardWindowTarget("dxgi_resize_failed");
        SetHresultError(lastError_, RES_STR("dxgi_resize_failed"), hr);
        return false;
    }
    dxgiWindowWidth_ = targetWidth;
    dxgiWindowHeight_ = targetHeight;
    dxgiWindowRetainContents_ = retainContents;
    dxgiRetainedBuffersPrimed_ = 0;
    d2dCache_.Clear();
    return CreateDxgiWindowTargetBitmap();
}

bool D2DRenderer::CreateDxgiWindowTargetBitmap() {
    if (dxgiSwapChain_ == nullptr || d2dDeviceContext_ == nullptr) {
        lastError_ = ResourceStringText(RES_STR("dxgi_target_missing"));
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISurface> backBuffer;
    HRESULT hr = dxgiSwapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr) || backBuffer == nullptr) {
        SetHresultError(lastError_, RES_STR("dxgi_back_buffer_failed"), hr);
        return false;
    }

    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f,
        96.0f);
    hr = d2dDeviceContext_->CreateBitmapFromDxgiSurface(
        backBuffer.Get(), properties, dxgiWindowTargetBitmap_.ReleaseAndGetAddressOf());
    if (FAILED(hr) || dxgiWindowTargetBitmap_ == nullptr) {
        SetHresultError(lastError_, RES_STR("dxgi_d2d_target_failed"), hr);
        return false;
    }
    d2dCache_.ResetTarget();
    panelIconAtlasMask_.Reset();
    panelIconAtlasMaskTarget_ = nullptr;
    return true;
}

bool D2DRenderer::PresentDxgiWindow() {
    if (dxgiSwapChain_ == nullptr) {
        return true;
    }

    // Live presentation is vsynced; benchmark immediate-present paths measure draw cost without monitor cadence.
    const HRESULT hr = dxgiSwapChain_->Present(d2dImmediatePresent_ ? 0 : 1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        DiscardWindowTarget("dxgi_device_lost");
        SetHresultError(lastError_, RES_STR("dxgi_present_device_lost"), hr);
        return false;
    }
    if (FAILED(hr)) {
        SetHresultError(lastError_, RES_STR("dxgi_present_failed"), hr);
        return false;
    }
    return true;
}

bool D2DRenderer::BeginDirect2DDraw(ID2D1RenderTarget* target, ActiveDrawTarget targetKind) {
    if (target == nullptr) {
        lastError_ = ResourceStringText(RES_STR("d2d_target_missing"));
        return false;
    }
    d2dCache_.AttachTarget(target);
    lastError_.clear();
    d2dActiveRenderTarget_ = target;
    d2dActiveDrawTarget_ = targetKind;
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
    const bool activeWindowTarget = d2dActiveDrawTarget_ == ActiveDrawTarget::Window;
    const HRESULT hr = d2dActiveRenderTarget_->EndDraw();
    d2dActiveRenderTarget_ = nullptr;
    d2dActiveDrawTarget_ = ActiveDrawTarget::None;
    if (!activeWindowTarget) {
        d2dCache_.ResetTarget();
        panelIconAtlasMask_.Reset();
        panelIconAtlasMaskTarget_ = nullptr;
    }
    if (activeWindowTarget && hr == D2DERR_RECREATE_TARGET) {
        DiscardWindowTarget("recreate_target");
    } else if (FAILED(hr)) {
        if (activeWindowTarget) {
            DiscardWindowTarget("end_draw_failed");
        }
        SetHresultError(lastError_, RES_STR("d2d_end_draw_failed"), hr);
    }
}

bool D2DRenderer::BeginWindowDraw(int width, int height, bool retainContents) {
    if (EnsureDxgiWindowTarget(width, height, retainContents) && d2dDeviceContext_ != nullptr) {
        d2dDeviceContext_->SetTarget(dxgiWindowTargetBitmap_.Get());
        return BeginDirect2DDraw(d2dDeviceContext_.Get(), ActiveDrawTarget::Window);
    }
    if (!lastError_.empty()) {
        return false;
    }
    if (!EnsureWindowRenderTarget(width, height, retainContents) || d2dWindowRenderTarget_ == nullptr) {
        return false;
    }
    return BeginDirect2DDraw(d2dWindowRenderTarget_.Get(), ActiveDrawTarget::Window);
}

void D2DRenderer::EndWindowDraw() {
    const bool presentingDxgi = d2dActiveRenderTarget_ == d2dDeviceContext_.Get() && dxgiSwapChain_ != nullptr;
    EndDirect2DDraw();
    if (presentingDxgi && lastError_.empty()) {
        PresentDxgiWindow();
    }
}

void D2DRenderer::DiscardWindowTarget(std::string_view) {
    d2dClipDepth_ = 0;
    if (d2dActiveDrawTarget_ == ActiveDrawTarget::Window) {
        d2dActiveRenderTarget_ = nullptr;
        d2dActiveDrawTarget_ = ActiveDrawTarget::None;
    }
    if (d2dDeviceContext_ != nullptr) {
        d2dDeviceContext_->SetTarget(nullptr);
    }
    dxgiWindowTargetBitmap_.Reset();
    dxgiSwapChain_.Reset();
    dxgiWindowRetainContents_ = false;
    dxgiWindowWidth_ = 0;
    dxgiWindowHeight_ = 0;
    dxgiRetainedBuffersPrimed_ = 0;
    d2dWindowRenderTarget_.Reset();
    d2dWindowRetainContents_ = false;
    d2dCache_.ResetTarget();
    panelIconAtlasMask_.Reset();
    panelIconAtlasMaskTarget_ = nullptr;
}

ID2D1SolidColorBrush* D2DRenderer::D2DSolidBrush(RenderColorId colorId) {
    if (d2dActiveRenderTarget_ == nullptr) {
        return nullptr;
    }
    return d2dCache_.SolidBrush(d2dActiveRenderTarget_, palette_, colorId);
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

Microsoft::WRL::ComPtr<ID2D1Bitmap> D2DRenderer::D2DBitmapForRenderBitmap(const RenderBitmap& bitmap) {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap;
    if (!IsDrawActive() || bitmap.Empty()) {
        return d2dBitmap;
    }
    const D2D1_BITMAP_PROPERTIES properties =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = E_FAIL;
    if (bitmap.resource != nullptr && bitmap.resource->TypeToken() == D2DRenderBitmapResourceTypeToken()) {
        const auto* resource = static_cast<const D2DRenderBitmapResource*>(bitmap.resource.get());
        if (resource->D2DBitmap() != nullptr) {
            d2dBitmap = resource->D2DBitmap();
            hr = S_OK;
        } else if (resource->WicBitmap() != nullptr) {
            if (ID2D1Bitmap* cachedBitmap = resource->CachedD2DBitmap(d2dActiveRenderTarget_);
                cachedBitmap != nullptr) {
                d2dBitmap = cachedBitmap;
                hr = S_OK;
            } else {
                hr = d2dActiveRenderTarget_->CreateSharedBitmap(
                    __uuidof(IWICBitmap), resource->WicBitmap(), &properties, d2dBitmap.GetAddressOf());
                if (FAILED(hr) || d2dBitmap == nullptr) {
                    d2dBitmap.Reset();
                    hr = d2dActiveRenderTarget_->CreateBitmapFromWicBitmap(
                        resource->WicBitmap(), &properties, d2dBitmap.GetAddressOf());
                }
                if (SUCCEEDED(hr) && d2dBitmap != nullptr) {
                    resource->CacheD2DBitmap(d2dActiveRenderTarget_, d2dBitmap);
                }
            }
        }
    }
    if (FAILED(hr) || d2dBitmap == nullptr) {
        SetHresultError(lastError_, RES_STR("draw_bitmap_create_failed"), hr);
        return {};
    }
    return d2dBitmap;
}

bool D2DRenderer::DrawBitmap(const RenderBitmap& bitmap, RenderPoint origin) {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap = D2DBitmapForRenderBitmap(bitmap);
    if (d2dBitmap == nullptr) {
        return false;
    }
    d2dActiveRenderTarget_->DrawBitmap(
        d2dBitmap.Get(),
        D2D1::RectF(
            static_cast<float>(origin.x),
            static_cast<float>(origin.y),
            static_cast<float>(origin.x + bitmap.width),
            static_cast<float>(origin.y + bitmap.height)),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    return true;
}

bool D2DRenderer::DrawBitmapRegion(const RenderBitmap& bitmap, const RenderRect& sourceRect, RenderPoint targetOrigin) {
    if (sourceRect.IsEmpty()) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap = D2DBitmapForRenderBitmap(bitmap);
    if (d2dBitmap == nullptr) {
        return false;
    }
    RenderRect clippedSource = sourceRect;
    clippedSource.left = std::clamp(clippedSource.left, 0, bitmap.width);
    clippedSource.top = std::clamp(clippedSource.top, 0, bitmap.height);
    clippedSource.right = std::clamp(clippedSource.right, 0, bitmap.width);
    clippedSource.bottom = std::clamp(clippedSource.bottom, 0, bitmap.height);
    if (clippedSource.IsEmpty()) {
        return false;
    }
    targetOrigin.x += clippedSource.left - sourceRect.left;
    targetOrigin.y += clippedSource.top - sourceRect.top;
    const D2D1_RECT_F destinationRect = D2D1::RectF(
        static_cast<float>(targetOrigin.x),
        static_cast<float>(targetOrigin.y),
        static_cast<float>(targetOrigin.x + clippedSource.Width()),
        static_cast<float>(targetOrigin.y + clippedSource.Height()));
    const D2D1_RECT_F sourceD2DRect = D2DRectFromRenderRect(clippedSource);
    d2dActiveRenderTarget_->DrawBitmap(
        d2dBitmap.Get(), destinationRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &sourceD2DRect);
    return true;
}

bool D2DRenderer::DrawBitmapRegions(const RenderBitmap& bitmap, std::span<const RenderRect> sourceRects) {
    if (sourceRects.empty()) {
        return true;
    }
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap = D2DBitmapForRenderBitmap(bitmap);
    if (d2dBitmap == nullptr) {
        return false;
    }
    const D2DRenderBitmapResource* resource = nullptr;
    if (bitmap.resource != nullptr && bitmap.resource->TypeToken() == D2DRenderBitmapResourceTypeToken()) {
        resource = static_cast<const D2DRenderBitmapResource*>(bitmap.resource.get());
    }
    ID2D1BitmapBrush* bitmapBrush =
        resource != nullptr ? resource->CachedD2DBitmapBrush(d2dActiveRenderTarget_) : nullptr;
    if (bitmapBrush == nullptr) {
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush> createdBrush;
        const HRESULT brushHr = d2dActiveRenderTarget_->CreateBitmapBrush(
            d2dBitmap.Get(),
            D2D1::BitmapBrushProperties(
                D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
            D2D1::BrushProperties(),
            createdBrush.GetAddressOf());
        if (FAILED(brushHr) || createdBrush == nullptr) {
            SetHresultError(lastError_, RES_STR("draw_bitmap_regions_brush_failed"), brushHr);
            return false;
        }
        bitmapBrush = createdBrush.Get();
        if (resource != nullptr) {
            resource->CacheD2DBitmapBrush(d2dActiveRenderTarget_, std::move(createdBrush));
        }
    }
    for (const RenderRect& sourceRect : sourceRects) {
        if (sourceRect.IsEmpty()) {
            continue;
        }
        RenderRect clippedSource = sourceRect;
        clippedSource.left = std::clamp(clippedSource.left, 0, bitmap.width);
        clippedSource.top = std::clamp(clippedSource.top, 0, bitmap.height);
        clippedSource.right = std::clamp(clippedSource.right, 0, bitmap.width);
        clippedSource.bottom = std::clamp(clippedSource.bottom, 0, bitmap.height);
        if (clippedSource.IsEmpty()) {
            continue;
        }
        const D2D1_RECT_F rect = D2DRectFromRenderRect(clippedSource);
        d2dActiveRenderTarget_->FillRectangle(rect, bitmapBrush);
    }
    return true;
}

bool D2DRenderer::DrawIcon(std::string_view iconName, const RenderRect& rect) {
    if (!IsDrawActive() || iconName.empty() || rect.IsEmpty()) {
        return false;
    }
    const int atlasSlot = GetPanelIconAtlasSlot(iconName);
    if (atlasSlot < 0) {
        return false;
    }
    if (panelIconAtlas_ == nullptr) {
        return true;
    }
    if (panelIconAtlasMaskTarget_ != d2dActiveRenderTarget_) {
        panelIconAtlasMask_.Reset();
        panelIconAtlasMaskTarget_ = d2dActiveRenderTarget_;
    }
    if (panelIconAtlasMask_ == nullptr) {
        panelIconAtlasMask_ = CreatePanelIconAtlasMaskBitmap(d2dActiveRenderTarget_, panelIconAtlas_.Get());
    }
    ID2D1SolidColorBrush* brush = D2DSolidBrush(RenderColorId::Icon);
    if (panelIconAtlasMask_ == nullptr || brush == nullptr) {
        return false;
    }
    const D2D1_RECT_F sourceRect = D2D1::RectF(
        0.0f,
        static_cast<float>(atlasSlot * kPanelIconAtlasCellSize),
        static_cast<float>(kPanelIconAtlasCellSize),
        static_cast<float>((atlasSlot + 1) * kPanelIconAtlasCellSize));
    const D2D1_ANTIALIAS_MODE previousAntialiasMode = d2dActiveRenderTarget_->GetAntialiasMode();
    // FillOpacityMask requires aliased antialias mode even though the icon edge alpha stays in the mask.
    d2dActiveRenderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    d2dActiveRenderTarget_->FillOpacityMask(
        panelIconAtlasMask_.Get(), brush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, D2DRectFromRenderRect(rect), sourceRect);
    d2dActiveRenderTarget_->SetAntialiasMode(previousAntialiasMode);
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
        D2D1::Ellipse(
            D2D1::Point2F(static_cast<float>(rect.left) + radiusX, static_cast<float>(rect.top) + radiusY),
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
    d2dActiveRenderTarget_->DrawRectangle(
        D2DRectFromRenderRect(rect),
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
    d2dActiveRenderTarget_->DrawEllipse(
        D2D1::Ellipse(D2DPointFromRenderPoint(rect.Center()), radiusX, radiusY),
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
    d2dActiveRenderTarget_->DrawLine(
        D2DPointFromRenderPoint(start),
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
    d2dActiveRenderTarget_->DrawGeometry(
        geometry,
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
    d2dActiveRenderTarget_->DrawGeometry(
        geometry.Get(),
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

    const std::wstring localeName = WideFromText(kLocaleName);
    const auto createFormat = [&](TextStyleId style) {
        UiFontConfig fontConfig = FontConfigForStyle(style_.fonts, style);
        fontConfig.size = ScaleLogical(fontConfig.size);
        const std::wstring face = WideFromText(fontConfig.face);
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        const HRESULT hr = dwriteFactory_->CreateTextFormat(
            face.c_str(),
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(fontConfig.weight),
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(fontConfig.size),
            localeName.c_str(),
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

TextLayoutResult D2DRenderer::MeasureTextBlockD2D(
    const RenderRect& rect,
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
    bool needsAtlas = false;
    for (const auto& iconName : style_.iconNames) {
        if (iconName.empty()) {
            continue;
        }
        if (GetPanelIconAtlasSlot(iconName) < 0) {
            lastError_ = FormatText(RES_STR("icon_unknown name=\"%s\""), iconName.c_str());
            ReleaseIcons();
            return false;
        }
        needsAtlas = true;
    }
    if (needsAtlas) {
        // Tests can render built-in layouts without linking app icon resources.
        panelIconAtlas_ = LoadPngResourceMask(wicFactory_.Get(), IDR_PANEL_ICONS);
    }
    return true;
}

void D2DRenderer::ReleaseIcons() {
    panelIconAtlas_ = nullptr;
    panelIconAtlasMask_.Reset();
    panelIconAtlasMaskTarget_ = nullptr;
}

bool D2DRenderer::RebuildTextFormatsAndMetrics() {
    if (!CreateDWriteTextFormats()) {
        lastError_ = ResourceStringText(RES_STR("text_format_create_failed"));
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
        if (FAILED(dwriteFactory_->CreateTextLayout(kTextMeasureSample, 2, format, 1024.0f, 1024.0f, &layout)) ||
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
