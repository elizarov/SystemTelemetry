#include "util/trace.h"

#include <windows.h>

#include <cstdio>

#include "util/lightweight_mutex.h"
#include "util/numeric_format.h"

namespace {

LightweightMutex& TraceWriteLock() {
    static LightweightMutex lock;
    return lock;
}

void WriteTraceLine(std::FILE* output, const char* prefix, const char* text) {
    const LightweightMutexLock lock(TraceWriteLock());
    std::string line = "[trace " + Trace::FormatTimestamp() + "] ";
    line += prefix;
    line += ":";
    line += text;
    line += "\n";
    fwrite(line.data(), 1, line.size(), output);
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

void Trace::Write(TracePrefix prefix, const std::string& text) const {
    Write(prefix, text.c_str());
}

const char* Trace::PrefixName(TracePrefix prefix) {
    switch (prefix) {
        case TracePrefix::AmdAdlx:
            return "amd_adlx";
        case TracePrefix::Crash:
            return "crash";
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
            text += ",";
        }
        text += PrefixName(static_cast<TracePrefix>(index));
    }
    return text;
}

const char* Trace::BoolText(bool value) {
    return value ? "yes" : "no";
}

std::string Trace::FormatTimestamp() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);
    char buffer[32];
    sprintf_s(buffer,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        localTime.wYear,
        localTime.wMonth,
        localTime.wDay,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds);
    return buffer;
}

std::string Trace::FormatValueDouble(const char* label, double value, int precision) {
    return std::string(label) + "=" + FormatDoubleFixed(value, precision);
}

std::string Trace::FormatPoint(int x, int y) {
    return std::to_string(x) + "," + std::to_string(y);
}

std::string Trace::EscapeText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string Trace::QuoteText(std::string_view text) {
    return "\"" + EscapeText(text) + "\"";
}
