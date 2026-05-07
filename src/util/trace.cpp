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

const char* TracePrefixText(TracePrefix prefix) {
    switch (prefix) {
        case TracePrefix::AmdAdlx:
            return "amd_adlx:";
        case TracePrefix::Crash:
            return "crash:";
        case TracePrefix::Diagnostics:
            return "diagnostics:";
        case TracePrefix::Fake:
            return "fake:";
        case TracePrefix::FpsEtw:
            return "fps_etw:";
        case TracePrefix::FpsProvider:
            return "fps_provider:";
        case TracePrefix::FpsServiceClient:
            return "fps_service_client:";
        case TracePrefix::GigabyteSiv:
            return "gigabyte_siv:";
        case TracePrefix::GpuVendor:
            return "gpu_vendor:";
        case TracePrefix::LayoutEditDialog:
            return "layout_edit_dialog:";
        case TracePrefix::LayoutEditDrag:
            return "layout_edit_drag:";
        case TracePrefix::LayoutEditHover:
            return "layout_edit_hover:";
        case TracePrefix::LayoutEditModal:
            return "layout_edit_modal:";
        case TracePrefix::LayoutEditMouseTracking:
            return "layout_edit_mouse_tracking:";
        case TracePrefix::LayoutEditTooltip:
            return "layout_edit_tooltip:";
        case TracePrefix::LayoutEditUi:
            return "layout_edit_ui:";
        case TracePrefix::LayoutSwitch:
            return "layout_switch:";
        case TracePrefix::MsiCenter:
            return "msi_center:";
        case TracePrefix::NvidiaNvml:
            return "nvidia_nvml:";
        case TracePrefix::Telemetry:
            return "telemetry:";
        case TracePrefix::UnsupportedBoard:
            return "unsupported_board:";
        case TracePrefix::UnsupportedGpu:
            return "unsupported_gpu:";
        case TracePrefix::Wallpaper:
            return "wallpaper:";
    }
    return "";
}

void WriteTraceLine(std::FILE* output, const char* prefix, const char* text) {
    const LightweightMutexLock lock(TraceWriteLock());
    std::string line = "[trace " + Trace::FormatTimestamp() + "] ";
    line += prefix;
    line += text;
    line += "\n";
    fwrite(line.data(), 1, line.size(), output);
    fflush(output);
}

}  // namespace

Trace::Trace(std::FILE* output) : output_(output) {}

void Trace::SetOutput(std::FILE* output) {
    output_ = output;
}

void Trace::Write(const char* text) const {
    if (output_ == nullptr) {
        return;
    }
    WriteTraceLine(output_, "", text);
}

void Trace::Write(const std::string& text) const {
    Write(text.c_str());
}

void Trace::Write(TracePrefix prefix, const char* text) const {
    if (output_ == nullptr) {
        return;
    }
    WriteTraceLine(output_, TracePrefixText(prefix), text);
}

void Trace::Write(TracePrefix prefix, const std::string& text) const {
    Write(prefix, text.c_str());
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
