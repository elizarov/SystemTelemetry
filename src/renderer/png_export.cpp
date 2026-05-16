#include "renderer/png_export.h"

#include <windows.h>

#include <limits>
#include <string>
#include <utility>
#include <wincodec.h>
#include <wrl/client.h>

#include "util/text_format.h"
#include "util/win32_format.h"

namespace {

bool SetError(std::string* errorText, std::string text) {
    if (errorText != nullptr) {
        *errorText = std::move(text);
    }
    return false;
}

bool SetHresultError(std::string* errorText, std::string_view prefix, HRESULT hr) {
    if (errorText != nullptr) {
        AssignFormat(*errorText, "%.*s hr=", static_cast<int>(prefix.size()), prefix.data());
        AppendHresult(*errorText, hr);
    }
    return false;
}

bool SetPrefixedHresultError(std::string* errorText, std::string_view prefix, const char* suffix, HRESULT hr) {
    if (errorText != nullptr) {
        AssignFormat(*errorText, "%.*s%s hr=", static_cast<int>(prefix.size()), prefix.data(), suffix);
        AppendHresult(*errorText, hr);
    }
    return false;
}

bool SetPrefixedHresultPathError(
    std::string* errorText, std::string_view prefix, const char* suffix, HRESULT hr, const FilePath& imagePath) {
    if (errorText != nullptr) {
        SetPrefixedHresultError(errorText, prefix, suffix, hr);
        AppendFormat(*errorText, " path=\"%s\"", imagePath.string().c_str());
    }
    return false;
}

const WICPixelFormatGUID& WicPixelFormat(PngPixelFormat pixelFormat) {
    switch (pixelFormat) {
        case PngPixelFormat::BgrOpaque:
            return GUID_WICPixelFormat24bppBGR;
        case PngPixelFormat::BgraWithAlpha:
            return GUID_WICPixelFormat32bppBGRA;
    }
    return GUID_WICPixelFormat32bppBGRA;
}

bool SaveBgraPngWithInitializedCom(
    const FilePath& imagePath, int width, int height, const std::vector<std::uint8_t>& bgra, std::string* errorText) {
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr) || factory == nullptr) {
        return SetHresultError(errorText, "png_wic_factory_failed", hr);
    }

    Microsoft::WRL::ComPtr<IWICBitmap> source;
    const UINT bitmapWidth = static_cast<UINT>(width);
    const UINT bitmapHeight = static_cast<UINT>(height);
    const UINT stride = bitmapWidth * 4u;
    const UINT byteCount = static_cast<UINT>(bgra.size());
    hr = factory->CreateBitmapFromMemory(bitmapWidth,
        bitmapHeight,
        GUID_WICPixelFormat32bppBGRA,
        stride,
        byteCount,
        const_cast<BYTE*>(bgra.data()),
        source.GetAddressOf());
    if (FAILED(hr) || source == nullptr) {
        return SetHresultError(errorText, "png_wic_bitmap_failed", hr);
    }

    return SaveWicBitmapSourcePng(
        factory.Get(), source.Get(), imagePath, PngPixelFormat::BgraWithAlpha, "png", errorText);
}

}  // namespace

bool SaveWicBitmapSourcePng(IWICImagingFactory* factory,
    IWICBitmapSource* source,
    const FilePath& imagePath,
    PngPixelFormat pixelFormat,
    std::string_view errorPrefix,
    std::string* errorText) {
    const std::string prefix(errorPrefix);
    if (factory == nullptr || source == nullptr) {
        return SetError(errorText, FormatText("%s_wic_unavailable", prefix.c_str()));
    }

    UINT bitmapWidth = 0;
    UINT bitmapHeight = 0;
    HRESULT hr = source->GetSize(&bitmapWidth, &bitmapHeight);
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_bitmap_size_failed", hr);
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    IWICBitmapSource* frameSource = source;
    const WICPixelFormatGUID targetPixelFormat = WicPixelFormat(pixelFormat);
    if (pixelFormat == PngPixelFormat::BgrOpaque) {
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr) || converter == nullptr) {
            return SetPrefixedHresultError(errorText, errorPrefix, "_converter_failed", hr);
        }
        hr = converter->Initialize(
            source, targetPixelFormat, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            return SetPrefixedHresultError(errorText, errorPrefix, "_converter_init_failed", hr);
        }
        frameSource = converter.Get();
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr) || stream == nullptr) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_stream_failed", hr);
    }
    const std::wstring wideImagePath = imagePath.Wide();
    hr = stream->InitializeFromFilename(wideImagePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        return SetPrefixedHresultPathError(errorText, errorPrefix, "_stream_open_failed", hr, imagePath);
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr) || encoder == nullptr) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_encoder_failed", hr);
    }
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_encoder_init_failed", hr);
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr) || frame == nullptr) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_frame_failed", hr);
    }
    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_frame_init_failed", hr);
    }
    hr = frame->SetSize(bitmapWidth, bitmapHeight);
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_frame_size_failed", hr);
    }

    WICPixelFormatGUID frameFormat = targetPixelFormat;
    hr = frame->SetPixelFormat(&frameFormat);
    if (FAILED(hr) || !IsEqualGUID(frameFormat, targetPixelFormat)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_frame_format_failed", hr);
    }
    hr = frame->WriteSource(frameSource, nullptr);
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_frame_write_failed", hr);
    }
    hr = frame->Commit();
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_frame_commit_failed", hr);
    }
    hr = encoder->Commit();
    if (FAILED(hr)) {
        return SetPrefixedHresultError(errorText, errorPrefix, "_encoder_commit_failed", hr);
    }
    return true;
}

bool SaveBgraPng(
    const FilePath& imagePath, int width, int height, const std::vector<std::uint8_t>& bgra, std::string* errorText) {
    if (width <= 0 || height <= 0) {
        return SetError(errorText, "png_invalid_size");
    }
    const std::size_t expectedBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (bgra.size() != expectedBytes || expectedBytes > static_cast<std::size_t>((std::numeric_limits<UINT>::max)())) {
        return SetError(errorText, "png_invalid_bitmap");
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return SetHresultError(errorText, "png_com_init_failed", initHr);
    }
    const bool shouldUninitialize = initHr == S_OK || initHr == S_FALSE;
    const bool ok = SaveBgraPngWithInitializedCom(imagePath, width, height, bgra, errorText);
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return ok;
}
