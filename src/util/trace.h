#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "util/trace_timing.h"

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
    Profile,
    Renderer,
    Telemetry,
    UnsupportedBoard,
    UnsupportedGpu,
    Wallpaper,
    Count,
};

class Trace {
public:
    explicit Trace(std::FILE* output = nullptr);

    void SetOutput(std::FILE* output);
    void SetEnabledPrefixes(std::uint64_t prefixes);
    bool Enabled() const;
    bool Enabled(TracePrefix prefix) const;
    TraceTimingCollector& Timings() const;

    void Write(TracePrefix prefix, const char* text) const;
    void Write(TracePrefix prefix, const std::string& text) const;
    void WriteFmt(TracePrefix prefix, const char* format, ...) const;
    void WriteVFmt(TracePrefix prefix, const char* format, va_list args) const;

    template <typename Builder> void WriteLazy(TracePrefix prefix, Builder&& builder) const {
        if (!Enabled(prefix)) {
            return;
        }
        Write(prefix, builder());
    }

    template <typename... Args> void WriteLazyFmt(TracePrefix prefix, const char* format, Args... args) const {
        if (!Enabled(prefix)) {
            return;
        }
        WriteFmt(prefix, format, args...);
    }

    static const char* PrefixName(TracePrefix prefix);
    static std::uint64_t PrefixMask(TracePrefix prefix);
    static std::uint64_t AllPrefixesMask();
    static std::optional<TracePrefix> ParsePrefixName(std::string_view name);
    static std::string PrefixNamesText();
    static const char* BoolText(bool value);
    static std::string FormatTimestamp();
    static std::string FormatValueDouble(const char* label, double value, int precision = 3);
    static std::string FormatPoint(int x, int y);
    static std::string EscapeText(std::string_view text);
    static std::string QuoteText(std::string_view text);

private:
    std::FILE* output_ = nullptr;
    std::uint64_t enabledPrefixes_ = AllPrefixesMask();
    mutable TraceTimingCollector timings_;
};

void WriteRendererErrorTrace(Trace& trace, std::string_view stage, const std::string& error);
