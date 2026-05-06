#include "util/trace.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "util/numeric_format.h"

namespace {

std::tm LocalTime(std::time_t time) {
#pragma warning(suppress : 4996)
    const std::tm* localTime = std::localtime(&time);
    return localTime != nullptr ? *localTime : std::tm{};
}

std::string FormatTraceTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    const std::tm localTime = LocalTime(std::chrono::system_clock::to_time_t(now));
    char buffer[32];
    sprintf_s(buffer,
        "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec,
        static_cast<long long>(milliseconds));
    return buffer;
}

std::mutex& TraceWriteMutex() {
    static std::mutex mutex;
    return mutex;
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
    const std::lock_guard lock(TraceWriteMutex());
    const std::string line = "[trace " + FormatTraceTimestamp() + "] " + text + "\n";
    fwrite(line.data(), 1, line.size(), output_);
    fflush(output_);
}

void Trace::Write(const std::string& text) const {
    Write(text.c_str());
}

const char* Trace::BoolText(bool value) {
    return value ? "yes" : "no";
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
