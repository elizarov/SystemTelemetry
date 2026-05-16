#pragma once

#include <cstdio>
#include <string>
#include <string_view>

enum class TracePrefix : unsigned char {
    AmdAdlx,
    BoardVendor,
    Crash,
    Diagnostics,
    Fake,
    FpsEtw,
    FpsProvider,
    FpsServiceClient,
    GigabyteSiv,
    GpuVendor,
    IntelLevelZero,
    LayoutEditDialog,
    LayoutEditDrag,
    LayoutEditHover,
    LayoutEditModal,
    LayoutEditMouseTracking,
    LayoutEditTooltip,
    LayoutEditUi,
    LayoutSwitch,
    MsiCenter,
    NvidiaNvml,
    Telemetry,
    UnsupportedBoard,
    UnsupportedGpu,
    Wallpaper,
};

class Trace {
public:
    explicit Trace(std::FILE* output = nullptr);

    void SetOutput(std::FILE* output);

    void Write(const char* text) const;
    void Write(const std::string& text) const;
    void Write(TracePrefix prefix, const char* text) const;
    void Write(TracePrefix prefix, const std::string& text) const;

    template <typename Builder> void WriteLazy(Builder&& builder) const {
        if (output_ == nullptr) {
            return;
        }
        Write(builder());
    }

    template <typename Builder> void WriteLazy(TracePrefix prefix, Builder&& builder) const {
        if (output_ == nullptr) {
            return;
        }
        Write(prefix, builder());
    }

    static const char* BoolText(bool value);
    static std::string FormatTimestamp();
    static std::string FormatValueDouble(const char* label, double value, int precision = 3);
    static std::string FormatPoint(int x, int y);
    static std::string EscapeText(std::string_view text);
    static std::string QuoteText(std::string_view text);

private:
    std::FILE* output_ = nullptr;
};
