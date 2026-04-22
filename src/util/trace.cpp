#include "util/trace.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>

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
    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
           << milliseconds;
    return output.str();
}

std::mutex& TraceWriteMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

Trace::Trace(std::ostream* output) : output_(output) {}

void Trace::SetOutput(std::ostream* output) {
    output_ = output;
}

void Trace::Write(const char* text) const {
    if (output_ == nullptr) {
        return;
    }
    const std::lock_guard lock(TraceWriteMutex());
    (*output_) << "[trace " << FormatTraceTimestamp() << "] " << text << '\n';
    output_->flush();
}

void Trace::Write(const std::string& text) const {
    Write(text.c_str());
}

std::string Trace::BoolText(bool value) {
    return value ? "yes" : "no";
}

std::string Trace::FormatValueDouble(const char* label, double value, int precision) {
    std::ostringstream output;
    output << label << '=' << std::fixed << std::setprecision(precision) << value;
    return output.str();
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
