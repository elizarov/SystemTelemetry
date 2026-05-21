#include "util/trace.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

#include "util/lightweight_mutex.h"
#include "util/numeric_format.h"
#include "util/text_format.h"

namespace {

LightweightMutex& TraceWriteLock() {
    static LightweightMutex lock;
    return lock;
}

void WriteTraceLine(std::FILE* output, const char* prefix, const char* text) {
    const LightweightMutexLock lock(TraceWriteLock());
    const std::string timestamp = Trace::FormatTimestamp();
    fprintf(output, "[trace %s] %s:%s\n", timestamp.c_str(), prefix, text);
    fflush(output);
}

}  // namespace

Trace::Trace(std::FILE* output) : output_(output) {}

void Trace::SetOutput(std::FILE* output) {
    if (output_ != nullptr && output_ != output) {
        timings_.Flush(*this);
    }
    output_ = output;
    if (output_ != nullptr) {
        timings_.Reset();
    }
}

void Trace::SetEnabledPrefixes(std::uint64_t prefixes) {
    if (prefixes == 0) {
        prefixes = AllPrefixesMask();
    }
    if (output_ != nullptr && enabledPrefixes_ != prefixes) {
        timings_.Flush(*this);
    }
    enabledPrefixes_ = prefixes;
    if (output_ != nullptr) {
        timings_.Reset();
    }
}

bool Trace::Enabled() const {
    return output_ != nullptr;
}

bool Trace::Enabled(TracePrefix prefix) const {
    return output_ != nullptr && (enabledPrefixes_ & PrefixMask(prefix)) != 0;
}

TraceTimingCollector& Trace::Timings() const {
    return timings_;
}

void Trace::Write(TracePrefix prefix, const char* text) const {
    if (!Enabled(prefix)) {
        return;
    }
    WriteTraceLine(output_, PrefixName(prefix), text);
}

void Trace::Write(TracePrefix prefix, ResourceStringId text) const {
    if (!Enabled(prefix)) {
        return;
    }
    WriteTraceLine(output_, PrefixName(prefix), ResourceStringText(text));
}

void Trace::Write(TracePrefix prefix, const std::string& text) const {
    Write(prefix, text.c_str());
}

void Trace::WriteFmt(TracePrefix prefix, const char* format, ...) const {
    va_list args;
    va_start(args, format);
    WriteVFmt(prefix, format, args);
    va_end(args);
}

void Trace::WriteFmt(TracePrefix prefix, ResourceStringId format, ...) const {
    va_list args;
    va_start(args, format);
    WriteVFmt(prefix, format, args);
    va_end(args);
}

void Trace::WriteVFmt(TracePrefix prefix, const char* format, va_list args) const {
    if (!Enabled(prefix)) {
        return;
    }

    const std::string text = FormatTextV(format, args);
    WriteTraceLine(output_, PrefixName(prefix), text.c_str());
}

void Trace::WriteVFmt(TracePrefix prefix, ResourceStringId format, va_list args) const {
    if (!Enabled(prefix)) {
        return;
    }

    const std::string text = FormatTextV(format, args);
    WriteTraceLine(output_, PrefixName(prefix), text.c_str());
}

const char* Trace::PrefixName(TracePrefix prefix) {
    switch (prefix) {
        case TracePrefix::AmdAdlx:
            return "amd_adlx";
        case TracePrefix::AsusArmouryCrate:
            return "asus_armoury_crate";
        case TracePrefix::BoardVendor:
            return "board_vendor";
        case TracePrefix::Crash:
            return "crash";
        case TracePrefix::DashboardTooltip:
            return "dashboard_tooltip";
        case TracePrefix::Diagnostics:
            return "diagnostics";
        case TracePrefix::Fake:
            return "fake";
        case TracePrefix::FpsEtw:
            return "fps_etw";
        case TracePrefix::FpsProvider:
            return "fps_provider";
        case TracePrefix::FpsServiceClient:
            return "fps_service_client";
        case TracePrefix::GigabyteSiv:
            return "gigabyte_siv";
        case TracePrefix::GpuVendor:
            return "gpu_vendor";
        case TracePrefix::IntelLevelZero:
            return "intel_level_zero";
        case TracePrefix::LayoutEditDialog:
            return "layout_edit_dialog";
        case TracePrefix::LayoutEditDrag:
            return "layout_edit_drag";
        case TracePrefix::LayoutEditHover:
            return "layout_edit_hover";
        case TracePrefix::LayoutEditModal:
            return "layout_edit_modal";
        case TracePrefix::LayoutEditMouseTracking:
            return "layout_edit_mouse_tracking";
        case TracePrefix::LayoutEditTooltip:
            return "layout_edit_tooltip";
        case TracePrefix::LayoutEditUi:
            return "layout_edit_ui";
        case TracePrefix::LayoutSwitch:
            return "layout_switch";
        case TracePrefix::MsiCenter:
            return "msi_center";
        case TracePrefix::NvidiaNvml:
            return "nvidia_nvml";
        case TracePrefix::Profile:
            return "profile";
        case TracePrefix::Renderer:
            return "renderer";
        case TracePrefix::Telemetry:
            return "telemetry";
        case TracePrefix::UnsupportedBoard:
            return "unsupported_board";
        case TracePrefix::UnsupportedGpu:
            return "unsupported_gpu";
        case TracePrefix::Wallpaper:
            return "wallpaper";
        case TracePrefix::Count:
            break;
    }
    return "";
}

std::uint64_t Trace::PrefixMask(TracePrefix prefix) {
    const auto index = static_cast<unsigned>(prefix);
    if (index >= static_cast<unsigned>(TracePrefix::Count)) {
        return 0;
    }
    return std::uint64_t{1} << index;
}

std::uint64_t Trace::AllPrefixesMask() {
    static_assert(static_cast<unsigned>(TracePrefix::Count) < 64);
    return (std::uint64_t{1} << static_cast<unsigned>(TracePrefix::Count)) - 1;
}

std::optional<TracePrefix> Trace::ParsePrefixName(std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    for (unsigned index = 0; index < static_cast<unsigned>(TracePrefix::Count); ++index) {
        const TracePrefix prefix = static_cast<TracePrefix>(index);
        if (PrefixName(prefix) == name) {
            return prefix;
        }
    }
    return std::nullopt;
}

std::string Trace::PrefixNamesText() {
    std::string text;
    for (unsigned index = 0; index < static_cast<unsigned>(TracePrefix::Count); ++index) {
        if (!text.empty()) {
            AppendFormat(text, ",");
        }
        AppendFormat(text, "%s", PrefixName(static_cast<TracePrefix>(index)));
    }
    return text;
}

const char* Trace::BoolText(bool value) {
    return value ? "yes" : "no";
}

std::string Trace::FormatTimestamp() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);
    return FormatText("%04u-%02u-%02u %02u:%02u:%02u.%03u",
        localTime.wYear,
        localTime.wMonth,
        localTime.wDay,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds);
}

std::string Trace::FormatValueDouble(const char* label, double value, int precision) {
    return FormatText("%s=%s", label, FormatDoubleFixed(value, precision).c_str());
}

void WriteRendererErrorTrace(Trace& trace, std::string_view stage, const std::string& error) {
    if (error.empty()) {
        return;
    }
    trace.WriteFmt(TracePrefix::Renderer,
        RES_STR("error stage=\"%.*s\" detail=\"%s\""),
        static_cast<int>(stage.size()),
        stage.data(),
        error.c_str());
}

void WriteRendererErrorTrace(Trace& trace, ResourceStringId stage, const std::string& error) {
    if (error.empty()) {
        return;
    }
    trace.WriteFmt(
        TracePrefix::Renderer, RES_STR("error stage=\"%s\" detail=\"%s\""), ResourceStringText(stage), error.c_str());
}
