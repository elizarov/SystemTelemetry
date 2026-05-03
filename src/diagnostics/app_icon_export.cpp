#include "diagnostics/app_icon_export.h"

#include <windows.h>

#include <cstdint>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

#include "util/strings.h"
#include "util/utf8.h"
#include "widget/app_icon_geometry.h"

namespace {

class ScopedComInitialize {
public:
    ScopedComInitialize() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

    ~ScopedComInitialize() {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }

    bool Failed() const {
        return FAILED(result_) && result_ != RPC_E_CHANGED_MODE;
    }

    HRESULT Result() const {
        return result_;
    }

private:
    HRESULT result_ = S_OK;
};

bool SetError(std::string* errorText, std::string text) {
    if (errorText != nullptr) {
        *errorText = std::move(text);
    }
    return false;
}

bool SaveBitmapAsCompressedPng(const AppIconBitmap& bitmap, const FilePath& imagePath, std::string* errorText) {
    const ScopedComInitialize com;
    if (com.Failed()) {
        return SetError(errorText, "app_icon:wic_com_init_failed hr=" + FormatHresult(com.Result()));
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr) || factory == nullptr) {
        return SetError(errorText, "app_icon:wic_factory_failed hr=" + FormatHresult(hr));
    }

    Microsoft::WRL::ComPtr<IWICBitmap> source;
    const UINT size = static_cast<UINT>(bitmap.size);
    const UINT stride = size * 4u;
    hr = factory->CreateBitmapFromMemory(size,
        size,
        GUID_WICPixelFormat32bppBGRA,
        stride,
        static_cast<UINT>(bitmap.bgra.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bitmap.bgra.data())),
        source.GetAddressOf());
    if (FAILED(hr) || source == nullptr) {
        return SetError(errorText, "app_icon:wic_bitmap_failed hr=" + FormatHresult(hr));
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr) || stream == nullptr) {
        return SetError(errorText, "app_icon:wic_stream_failed hr=" + FormatHresult(hr));
    }

    hr = stream->InitializeFromFilename(imagePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        return SetError(errorText,
            "app_icon:wic_stream_open_failed hr=" + FormatHresult(hr) + " path=\"" + Utf8FromWide(imagePath.wstring()) +
                "\"");
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr) || encoder == nullptr) {
        return SetError(errorText, "app_icon:wic_encoder_failed hr=" + FormatHresult(hr));
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_encoder_init_failed hr=" + FormatHresult(hr));
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr) || frame == nullptr) {
        return SetError(errorText, "app_icon:wic_frame_failed hr=" + FormatHresult(hr));
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_frame_init_failed hr=" + FormatHresult(hr));
    }

    hr = frame->SetSize(size, size);
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_frame_size_failed hr=" + FormatHresult(hr));
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_frame_format_failed hr=" + FormatHresult(hr));
    }

    hr = frame->WriteSource(source.Get(), nullptr);
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_frame_write_failed hr=" + FormatHresult(hr));
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_frame_commit_failed hr=" + FormatHresult(hr));
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        return SetError(errorText, "app_icon:wic_commit_failed hr=" + FormatHresult(hr));
    }

    return true;
}

}  // namespace

bool SaveAppIconPng(const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText) {
    if (!IsValidAppIconSize(size)) {
        if (errorText != nullptr) {
            *errorText = "app_icon:invalid_size";
        }
        return false;
    }

    const AppIconBitmap bitmap = RenderAppIconBitmap(config, size);
    return SaveBitmapAsCompressedPng(bitmap, imagePath, errorText);
}
