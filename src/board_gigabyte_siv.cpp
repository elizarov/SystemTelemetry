#define NOMINMAX
#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "board_vendor.h"
#include "trace.h"
#include "utf8.h"

namespace {

struct ProbeResult {
    bool success = false;
    std::string diagnostics;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct FanReading {
    std::string title;
    std::optional<double> rpm;
};

std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG probe = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (probe != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LONG status = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, value.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return Utf8FromWide(value);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsInsensitive(const std::string& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::string::npos;
}

std::wstring AbsolutePath(const std::wstring& relativePath) {
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    std::wstring path(modulePath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash + 1);
    }
    path += relativePath;

    wchar_t full[MAX_PATH];
    const DWORD length = GetFullPathNameW(path.c_str(), ARRAYSIZE(full), full, nullptr);
    if (length == 0 || length >= ARRAYSIZE(full)) {
        return path;
    }
    return std::wstring(full, length);
}

std::string Trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::optional<uint32_t> ParseUnsigned(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long value = strtoul(text.c_str(), &end, 0);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(value);
}

std::optional<double> ParseDouble(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = strtod(text.c_str(), &end);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) {
        return std::nullopt;
    }
    return value;
}

ProbeResult RunProbeHelper(const std::wstring& helperPath, tracing::Trace& trace) {
    ProbeResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.diagnostics = "Failed to create helper pipe.";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine = L"\"" + helperPath + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        result.diagnostics = "Failed to start GigabyteFanProbe helper.";
        return result;
    }

    std::string output;
    char buffer[512];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, buffer + read);
    }
    CloseHandle(readPipe);

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (waitResult != WAIT_OBJECT_0) {
        result.diagnostics = "GigabyteFanProbe helper timed out.";
        return result;
    }

    trace.Write("gigabyte_siv:helper_output_begin");
    trace.Write(output);
    trace.Write("gigabyte_siv:helper_output_end exit_code=" + std::to_string(exitCode));

    size_t position = 0;
    while (position < output.size()) {
        const size_t end = output.find('\n', position);
        std::string line = Trim(output.substr(position, end == std::string::npos ? std::string::npos : end - position));
        position = end == std::string::npos ? output.size() : end + 1;
        if (line.empty()) {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        result.fields.emplace_back(line.substr(0, equals), line.substr(equals + 1));
    }

    for (const auto& [key, value] : result.fields) {
        if (key == "success") {
            result.success = value == "1" || value == "true" || value == "yes";
        } else if (key == "diagnostics") {
            result.diagnostics = value;
        }
    }
    if (result.diagnostics.empty() && exitCode != 0) {
        result.diagnostics = "GigabyteFanProbe helper returned failure.";
    }
    return result;
}

class GigabyteSivBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit GigabyteSivBoardTelemetryProvider(tracing::Trace* trace) : trace_(trace) {}

    bool Initialize(const AppConfig& config) override {
        config_ = config;
        trace().Write("gigabyte_siv:initialize_begin");

        boardManufacturer_ = ReadRegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS",
            L"BaseBoardManufacturer").value_or("");
        boardProduct_ = ReadRegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS",
            L"BaseBoardProduct").value_or("");
        trace().Write("gigabyte_siv:board manufacturer=\"" + boardManufacturer_ + "\" product=\"" + boardProduct_ + "\"");

        if (!ContainsInsensitive(boardManufacturer_, "gigabyte")) {
            diagnostics_ = "Baseboard manufacturer is not Gigabyte.";
            return false;
        }

        helperPath_ = AbsolutePath(L"GigabyteFanProbe.exe");
        if (GetFileAttributesW(helperPath_.c_str()) == INVALID_FILE_ATTRIBUTES) {
            diagnostics_ = "GigabyteFanProbe helper was not built.";
            trace().Write("gigabyte_siv:helper_missing path=\"" + Utf8FromWide(helperPath_) + "\"");
            return false;
        }

        loadedLibrary_ = Utf8FromWide(helperPath_);
        diagnostics_ = "Gigabyte helper ready.";
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Gigabyte";
        sample.requestedFanChannel = config_.gigabyteFanChannel;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.chipName = chipName_;
        sample.controllerType = controllerType_;
        sample.driverLibrary = loadedLibrary_;
        sample.selectedFanChannel = selectedChannel_ > 0 ? std::optional<int>(selectedChannel_) : std::nullopt;
        sample.chipId = chipId_;
        sample.monitorBaseAddress = monitorBaseAddress_;
        sample.ecMmioRegisterValue = ecMmioRegisterValue_;
        sample.fan16BitMode = true;
        sample.diagnostics = diagnostics_;

        if (!initialized_) {
            return sample;
        }

        const ULONGLONG now = GetTickCount64();
        if (cachedResult_.fields.empty() || now - lastProbeTick_ >= 1000) {
            cachedResult_ = RunProbeHelper(helperPath_, trace());
            lastProbeTick_ = now;
            ApplyProbeFields(cachedResult_);
        }

        sample.chipName = chipName_;
        sample.controllerType = controllerType_;
        sample.selectedFanChannel = selectedChannel_ > 0 ? std::optional<int>(selectedChannel_) : std::nullopt;
        sample.chipId = chipId_;
        sample.monitorBaseAddress = monitorBaseAddress_;
        sample.ecMmioRegisterValue = ecMmioRegisterValue_;
        sample.diagnostics = diagnostics_;

        if (!cachedResult_.success || fanReadings_.empty()) {
            return sample;
        }

        int chosenIndex = config_.gigabyteFanChannel >= 1 ? (config_.gigabyteFanChannel - 1) : 0;
        if (chosenIndex < 0 || chosenIndex >= static_cast<int>(fanReadings_.size()) || !fanReadings_[chosenIndex].rpm.has_value()) {
            chosenIndex = 0;
            for (int i = 0; i < static_cast<int>(fanReadings_.size()); ++i) {
                if (fanReadings_[i].rpm.has_value() && *fanReadings_[i].rpm > 0.0) {
                    chosenIndex = i;
                    break;
                }
            }
        }

        if (chosenIndex < 0 || chosenIndex >= static_cast<int>(fanReadings_.size()) ||
            !fanReadings_[chosenIndex].rpm.has_value() || *fanReadings_[chosenIndex].rpm <= 0.0) {
            return sample;
        }

        selectedChannel_ = chosenIndex + 1;
        sample.available = true;
        sample.fanRpm = fanReadings_[chosenIndex].rpm;
        sample.selectedFanChannel = selectedChannel_;
        sample.diagnostics = diagnostics_ + " sampled_title=" + fanReadings_[chosenIndex].title;
        return sample;
    }

private:
    tracing::Trace& trace() {
        static tracing::Trace nullTrace;
        return trace_ != nullptr ? *trace_ : nullTrace;
    }

    void ApplyProbeFields(const ProbeResult& result) {
        diagnostics_ = result.diagnostics.empty() ? "GigabyteFanProbe completed." : result.diagnostics;
        chipName_.clear();
        controllerType_.clear();
        chipId_.reset();
        monitorBaseAddress_.reset();
        ecMmioRegisterValue_.reset();
        fanReadings_.clear();

        for (const auto& [key, value] : result.fields) {
            if (key == "controller_type") {
                controllerType_ = value;
            } else if (key == "chip_name") {
                chipName_ = value;
            } else if (key == "chip_id") {
                if (auto parsed = ParseUnsigned(value)) {
                    chipId_ = static_cast<uint16_t>(*parsed);
                }
            } else if (key == "ecmmio_base") {
                monitorBaseAddress_ = ParseUnsigned(value);
            } else if (key == "ecmmio_register") {
                if (auto parsed = ParseUnsigned(value)) {
                    ecMmioRegisterValue_ = static_cast<uint8_t>(*parsed);
                }
            } else if (key.rfind("fan_", 0) == 0) {
                const size_t secondUnderscore = key.find('_', 4);
                if (secondUnderscore == std::string::npos) {
                    continue;
                }
                const auto parsedIndex = ParseUnsigned(key.substr(4, secondUnderscore - 4));
                if (!parsedIndex.has_value()) {
                    continue;
                }
                const int index = static_cast<int>(*parsedIndex);
                if (index >= static_cast<int>(fanReadings_.size())) {
                    fanReadings_.resize(index + 1);
                }
                const std::string suffix = key.substr(secondUnderscore + 1);
                if (suffix == "title") {
                    fanReadings_[index].title = value;
                } else if (suffix == "rpm") {
                    fanReadings_[index].rpm = ParseDouble(value);
                }
            }
        }

        if (chipName_.empty() && !controllerType_.empty()) {
            chipName_ = controllerType_;
        }
    }

    tracing::Trace* trace_ = nullptr;
    AppConfig config_{};
    std::wstring helperPath_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string chipName_;
    std::string controllerType_;
    std::string loadedLibrary_;
    std::string diagnostics_ = "Gigabyte provider not initialized.";
    std::optional<uint16_t> chipId_;
    std::optional<uint32_t> monitorBaseAddress_;
    std::optional<uint8_t> ecMmioRegisterValue_;
    std::vector<FanReading> fanReadings_;
    ProbeResult cachedResult_{};
    ULONGLONG lastProbeTick_ = 0;
    int selectedChannel_ = 0;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(tracing::Trace* trace) {
    return std::make_unique<GigabyteSivBoardTelemetryProvider>(trace);
}
