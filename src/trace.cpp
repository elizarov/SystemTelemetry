#include "trace.h"

#include <cstdio>

namespace tracing {

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
    (*output_) << "[trace] " << text << '\n';
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

}  // namespace tracing
