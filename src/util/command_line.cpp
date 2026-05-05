#include "util/command_line.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <shellapi.h>

std::wstring TrimWhitespace(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return iswspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::wstring(first, last);
}

std::wstring StripOuterQuotes(std::wstring value) {
    value = TrimWhitespace(std::move(value));
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::wstring NormalizeWindowsPath(std::wstring value) {
    value = StripOuterQuotes(std::move(value));
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(
        value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

namespace {

class CommandLineArguments {
public:
    CommandLineArguments() : argv_(CommandLineToArgvW(GetCommandLineW(), &argc_)) {}

    ~CommandLineArguments() {
        if (argv_ != nullptr) {
            LocalFree(argv_);
        }
    }

    int Count() const {
        return argv_ != nullptr ? argc_ : 0;
    }

    const wchar_t* At(int index) const {
        return argv_[index];
    }

private:
    int argc_ = 0;
    LPWSTR* argv_ = nullptr;
};

}  // namespace

std::wstring BuildCommandLineExcludingSwitch(const wchar_t* excludedSwitch) {
    // Size: command-line callers only scan argv; avoid exposing vector<wstring> for one relaunch helper.
    CommandLineArguments arguments;
    std::wstring parameters;
    for (int i = 1; i < arguments.Count(); ++i) {
        const wchar_t* argument = arguments.At(i);
        if (excludedSwitch != nullptr && _wcsicmp(argument, excludedSwitch) == 0) {
            continue;
        }
        if (!parameters.empty()) {
            parameters += L' ';
        }
        parameters += QuoteCommandLineArgument(argument);
    }
    return parameters;
}

bool HasSwitch(const wchar_t* target) {
    CommandLineArguments arguments;
    for (int i = 1; i < arguments.Count(); ++i) {
        if (_wcsicmp(arguments.At(i), target) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::wstring> GetSwitchValue(const wchar_t* target) {
    CommandLineArguments arguments;
    for (int i = 1; i + 1 < arguments.Count(); ++i) {
        if (_wcsicmp(arguments.At(i), target) == 0) {
            return arguments.At(i + 1);
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> GetColonSwitchValue(const wchar_t* target) {
    CommandLineArguments arguments;
    const size_t targetLength = std::wcslen(target);
    for (int i = 1; i < arguments.Count(); ++i) {
        const wchar_t* argument = arguments.At(i);
        const size_t argumentLength = std::wcslen(argument);
        if (argumentLength > targetLength && _wcsnicmp(argument, target, targetLength) == 0 &&
            argument[targetLength] == L':') {
            return argument + targetLength + 1;
        }
    }
    return std::nullopt;
}
