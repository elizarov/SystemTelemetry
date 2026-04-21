#include "trace.h"

#include <cstdio>
#include <windows.h>

namespace tracing {

namespace {

std::string FormatTraceTimestamp() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);

    char buffer[40];
    sprintf_s(buffer,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        static_cast<unsigned>(localTime.wYear),
        static_cast<unsigned>(localTime.wMonth),
        static_cast<unsigned>(localTime.wDay),
        static_cast<unsigned>(localTime.wHour),
        static_cast<unsigned>(localTime.wMinute),
        static_cast<unsigned>(localTime.wSecond),
        static_cast<unsigned>(localTime.wMilliseconds));
    return buffer;
}

}  // namespace

Trace::Trace(std::ostream* output) : output_(output) {}

void Trace::SetOutput(std::ostream* output) {
    output_ = output;
}

std::ostream* Trace::Output() const {
    return output_;
}

bool Trace::Enabled() const {
    return output_ != nullptr;
}

void Trace::Write(const char* text) const {
    if (output_ == nullptr) {
        return;
    }
    (*output_) << "[trace " << FormatTraceTimestamp() << "] " << text << '\n';
    output_->flush();
}

void Trace::Write(const std::string& text) const {
    Write(text.c_str());
}

std::string Trace::BoolText(bool value) {
    return value ? "yes" : "no";
}

std::string Trace::FormatAdlxResult(const char* label, int result) {
    char buffer[64];
    sprintf_s(buffer, "%s=%d", label, result);
    return buffer;
}

std::string Trace::FormatPdhStatus(const char* label, long status) {
    char buffer[64];
    sprintf_s(buffer, "%s=%ld", label, status);
    return buffer;
}

std::string Trace::FormatWin32Status(const char* label, unsigned long status) {
    char buffer[64];
    sprintf_s(buffer, "%s=%lu", label, status);
    return buffer;
}

std::string Trace::FormatValueDouble(const char* label, double value, int precision) {
    char buffer[96];
    sprintf_s(buffer, "%s=%.*f", label, precision, value);
    return buffer;
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

}  // namespace tracing
