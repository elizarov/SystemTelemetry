#pragma once

#include <ostream>
#include <string>

namespace tracing {

class Trace {
public:
    explicit Trace(std::ostream* output = nullptr);

    void SetOutput(std::ostream* output);
    std::ostream* Output() const;
    bool Enabled() const;

    void Write(const char* text) const;
    void Write(const std::string& text) const;

    template <typename Builder> void WriteLazy(Builder&& builder) const {
        if (output_ == nullptr) {
            return;
        }
        Write(builder());
    }

    static std::string BoolText(bool value);
    static std::string FormatAdlxResult(const char* label, int result);
    static std::string FormatPdhStatus(const char* label, long status);
    static std::string FormatWin32Status(const char* label, unsigned long status);
    static std::string FormatValueDouble(const char* label, double value, int precision = 3);

private:
    std::ostream* output_ = nullptr;
};

}  // namespace tracing
