#include "headless/headless_command_line.h"

#include <cctype>
#include <cstdlib>
#include <limits>
#include <utility>

#include "diagnostics/diagnostics.h"
#include "util/text_format.h"

namespace {

enum class HeadlessSwitchValue {
    None,
    Optional,
    Required,
    EditLayout,
};

struct HeadlessSwitchSpec {
    const char* name;
    HeadlessSwitchValue value;
};

constexpr HeadlessSwitchSpec kHeadlessSwitches[] = {
    {"/trace", HeadlessSwitchValue::Optional},
    {"/trace-prefixes", HeadlessSwitchValue::Required},
    {"/dump", HeadlessSwitchValue::Optional},
    {"/screenshot", HeadlessSwitchValue::Optional},
    {"/layout-guide-sheet", HeadlessSwitchValue::Optional},
    {"/app-icon", HeadlessSwitchValue::Optional},
    {"/save-config", HeadlessSwitchValue::Optional},
    {"/save-full-config", HeadlessSwitchValue::Optional},
    {"/reload", HeadlessSwitchValue::None},
    {"/fake", HeadlessSwitchValue::Optional},
    {"/layout", HeadlessSwitchValue::Required},
    {"/theme", HeadlessSwitchValue::Required},
    {"/default-config", HeadlessSwitchValue::None},
    {"/scale", HeadlessSwitchValue::Required},
    {"/app-icon-size", HeadlessSwitchValue::Required},
    {"/blank", HeadlessSwitchValue::None},
    {"/edit-layout", HeadlessSwitchValue::EditLayout},
    {"/hover", HeadlessSwitchValue::Required},
    {"/exit", HeadlessSwitchValue::None},
};

char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (LowerAscii(left[i]) != LowerAscii(right[i])) {
            return false;
        }
    }
    return true;
}

std::string_view TrimAsciiView(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string FormatArgument(std::string_view argument) {
    if (argument.empty()) {
        return "<empty>";
    }
    return FormatText("\"%.*s\"", static_cast<int>(argument.size()), argument.data());
}

HeadlessCommandLineValidationResult HeadlessCommandLineFailure(std::string message) {
    HeadlessCommandLineValidationResult result;
    result.ok = false;
    result.message = std::move(message);
    return result;
}

HeadlessCommandLineValidationResult HeadlessCommandLineHelpRequest() {
    HeadlessCommandLineValidationResult result;
    result.requestedHelp = true;
    return result;
}

bool IsHelpSwitch(std::string_view argument) {
    return EqualsAsciiInsensitive(argument, "/?") || EqualsAsciiInsensitive(argument, "/help") ||
           EqualsAsciiInsensitive(argument, "-h") || EqualsAsciiInsensitive(argument, "--help");
}

const HeadlessSwitchSpec* FindHeadlessSwitch(std::string_view name) {
    for (const HeadlessSwitchSpec& spec : kHeadlessSwitches) {
        if (EqualsAsciiInsensitive(name, spec.name)) {
            return &spec;
        }
    }
    return nullptr;
}

bool TryParseInteger(std::string_view text, int& parsedValue) {
    text = TrimAsciiView(text);
    if (text.empty()) {
        return false;
    }
    std::string owned(text);
    char* end = nullptr;
    const long value = std::strtol(owned.c_str(), &end, 10);
    if (end == owned.c_str() || end == nullptr || *end != '\0' || value < (std::numeric_limits<int>::min)() ||
        value > (std::numeric_limits<int>::max)()) {
        return false;
    }
    parsedValue = static_cast<int>(value);
    return true;
}

bool TryParseHoverValue(std::string_view value) {
    const size_t comma = value.find(',');
    if (comma == std::string_view::npos || value.find(',', comma + 1) != std::string_view::npos) {
        return false;
    }

    int x = 0;
    int y = 0;
    return TryParseInteger(value.substr(0, comma), x) && TryParseInteger(value.substr(comma + 1), y);
}

std::string ValidateHeadlessSwitchValue(const HeadlessSwitchSpec& spec, bool hasColon, std::string_view value) {
    if (spec.value == HeadlessSwitchValue::None && hasColon) {
        return FormatText("%s does not accept a value.", spec.name);
    }

    if (spec.value == HeadlessSwitchValue::Required) {
        if (!hasColon || TrimAsciiView(value).empty()) {
            return FormatText("%s requires a value in the form %s:<value>.", spec.name, spec.name);
        }
    }

    if (EqualsAsciiInsensitive(spec.name, "/scale") && !TryParseScaleValue(std::string(value)).has_value()) {
        return FormatText("Invalid value for /scale: %s. Use a positive number.", FormatArgument(value).c_str());
    }

    if (EqualsAsciiInsensitive(spec.name, "/app-icon-size") &&
        !TryParseAppIconSizeValue(std::string(value)).has_value()) {
        return FormatText("Invalid value for /app-icon-size: %s. Use a pixel size from 16 through 1024.",
            FormatArgument(value).c_str());
    }

    if (EqualsAsciiInsensitive(spec.name, "/hover") && !TryParseHoverValue(value)) {
        return FormatText(
            "Invalid value for /hover: %s. Use x,y dashboard client coordinates.", FormatArgument(value).c_str());
    }

    return {};
}

}  // namespace

HeadlessCommandLineValidationResult ValidateHeadlessCommandLine(const CommandLineArguments& commandLine) {
    for (size_t i = 1; i < commandLine.size(); ++i) {
        const std::string& argument = commandLine[i];
        if (IsHelpSwitch(argument)) {
            return HeadlessCommandLineHelpRequest();
        }

        const size_t colon = argument.find(':');
        const bool hasColon = colon != std::string::npos;
        const std::string_view switchName =
            hasColon ? std::string_view(argument).substr(0, colon) : std::string_view(argument);
        const std::string_view switchValue =
            hasColon ? std::string_view(argument).substr(colon + 1) : std::string_view{};
        const HeadlessSwitchSpec* spec = FindHeadlessSwitch(switchName);
        if (spec == nullptr) {
            return HeadlessCommandLineFailure(
                FormatText("Unknown command-line parameter: %s.", FormatArgument(argument).c_str()));
        }

        const std::string error = ValidateHeadlessSwitchValue(*spec, hasColon, switchValue);
        if (!error.empty()) {
            return HeadlessCommandLineFailure(error);
        }
    }
    return {};
}

void PrintHeadlessCommandLineHelp(std::FILE* stream, std::string_view message) {
    if (stream == nullptr) {
        return;
    }
    if (!message.empty()) {
        std::fprintf(stream, "Error: %.*s\n\n", static_cast<int>(message.size()), message.data());
    }
    std::fputs("CaseDashHeadless.exe runs CaseDash diagnostics once and exits.\n"
               "\n"
               "Usage:\n"
               "  CaseDashHeadless.exe [switches]\n"
               "\n"
               "Output switches:\n"
               "  /trace[:path]                 Write trace output.\n"
               "  /trace-prefixes:<names>       Enable trace and filter comma-separated trace prefixes.\n"
               "  /dump[:path]                  Write a snapshot dump.\n"
               "  /screenshot[:path]            Write a dashboard PNG.\n"
               "  /layout-guide-sheet[:path]    Write a diagnostics layout guide sheet PNG.\n"
               "  /app-icon[:path]              Write the rendered app icon PNG.\n"
               "  /save-config[:path]           Write the minimal config overlay.\n"
               "  /save-full-config[:path]      Write the full config export.\n"
               "\n"
               "Source and config switches:\n"
               "  /reload                       Reload config before exports.\n"
               "  /fake[:path]                  Use built-in synthetic telemetry or a snapshot dump file.\n"
               "  /layout:<name>                Select a named layout.\n"
               "  /theme:<name>                 Select a named theme.\n"
               "  /default-config               Ignore executable-side config.ini for this run.\n"
               "  /scale:<value>                Override render scale with a positive number.\n"
               "  /app-icon-size:<pixels>       Set /app-icon size from 16 through 1024.\n"
               "\n"
               "Render switches:\n"
               "  /blank                        Export blank rendering mode.\n"
               "  /edit-layout[:target]         Enable layout-edit guides or a target preview.\n"
               "  /hover:<x>,<y>                Apply a layout-edit hover point during screenshot export.\n"
               "\n"
               "Control switches:\n"
               "  /exit                         Accepted for parity; headless always exits after one run.\n"
               "  /help or /?                   Print this help.\n"
               "\n"
               "Examples:\n"
               "  CaseDashHeadless.exe /default-config /fake /screenshot:build\\diagnostics\\screen.png\n"
               "  CaseDashHeadless.exe /default-config /fake /layout-guide-sheet:build\\diagnostics\\guide.png\n",
        stream);
}
