#define NOMINMAX
#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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

struct TemperatureReading {
    std::string title;
    std::optional<double> celsius;
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

struct DaemonProcess {
    HANDLE readPipe = nullptr;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
};

void CloseDaemonProcess(DaemonProcess& daemon) {
    if (daemon.readPipe != nullptr) {
        CloseHandle(daemon.readPipe);
        daemon.readPipe = nullptr;
    }
    if (daemon.thread != nullptr) {
        CloseHandle(daemon.thread);
        daemon.thread = nullptr;
    }
    if (daemon.process != nullptr) {
        TerminateProcess(daemon.process, 0);
        CloseHandle(daemon.process);
        daemon.process = nullptr;
    }
}

bool StartProbeDaemon(const std::wstring& helperPath, tracing::Trace& trace, DaemonProcess& daemon, std::string& diagnostics) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        diagnostics = "Failed to create helper pipe.";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine = L"\"" + helperPath + L"\" /daemon";
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
        diagnostics = "Failed to start GigabyteFanProbe daemon.";
        return false;
    }

    daemon.readPipe = readPipe;
    daemon.process = pi.hProcess;
    daemon.thread = pi.hThread;
    diagnostics = "GigabyteFanProbe daemon started.";
    trace.Write("gigabyte_siv:daemon_started path=\"" + Utf8FromWide(helperPath) + "\"");
    return true;
}

void ApplyResultField(ProbeResult& result, const std::string& line) {
    if (line.empty()) {
        return;
    }
    const size_t equals = line.find('=');
    if (equals == std::string::npos) {
        return;
    }
    const std::string key = line.substr(0, equals);
    const std::string value = line.substr(equals + 1);
    result.fields.emplace_back(key, value);
    if (key == "success") {
        result.success = value == "1" || value == "true" || value == "yes";
    } else if (key == "diagnostics") {
        result.diagnostics = value;
    }
}

bool PumpProbeDaemon(DaemonProcess& daemon, tracing::Trace& trace, std::string& buffer,
    ProbeResult& currentFrame, bool& frameOpen, std::deque<ProbeResult>& completedFrames, std::string& diagnostics) {
    if (daemon.readPipe == nullptr || daemon.process == nullptr) {
        diagnostics = "GigabyteFanProbe daemon is not running.";
        return false;
    }

    DWORD exitCode = STILL_ACTIVE;
    if (!GetExitCodeProcess(daemon.process, &exitCode) || exitCode != STILL_ACTIVE) {
        diagnostics = "GigabyteFanProbe daemon exited.";
        return false;
    }

    DWORD available = 0;
    if (!PeekNamedPipe(daemon.readPipe, nullptr, 0, nullptr, &available, nullptr)) {
        diagnostics = "Failed to poll GigabyteFanProbe daemon output.";
        return false;
    }
    if (available == 0) {
        return true;
    }

    std::string chunk;
    chunk.resize(available);
    DWORD read = 0;
    if (!ReadFile(daemon.readPipe, chunk.data(), available, &read, nullptr) || read == 0) {
        diagnostics = "Failed to read GigabyteFanProbe daemon output.";
        return false;
    }
    chunk.resize(read);
    trace.Write("gigabyte_siv:daemon_chunk_begin");
    trace.Write(chunk);
    trace.Write("gigabyte_siv:daemon_chunk_end size=" + std::to_string(read));
    buffer += chunk;

    size_t position = 0;
    while (position < buffer.size()) {
        const size_t end = buffer.find('\n', position);
        if (end == std::string::npos) {
            break;
        }
        const std::string line = Trim(buffer.substr(position, end - position));
        position = end + 1;
        if (line == "frame_begin") {
            currentFrame = ProbeResult{};
            frameOpen = true;
            continue;
        }
        if (line == "frame_end") {
            if (frameOpen) {
                completedFrames.push_back(currentFrame);
            }
            currentFrame = ProbeResult{};
            frameOpen = false;
            continue;
        }
        if (frameOpen) {
            ApplyResultField(currentFrame, line);
        }
    }
    buffer.erase(0, position);
    return true;
}

class GigabyteSivBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit GigabyteSivBoardTelemetryProvider(tracing::Trace* trace) : trace_(trace) {}
    ~GigabyteSivBoardTelemetryProvider() override {
        CloseDaemonProcess(daemon_);
    }

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
        sample.selectedCpuTemperatureSensor = selectedCpuTemperatureSensor_;
        sample.selectedFanChannel = selectedChannel_ > 0 ? std::optional<int>(selectedChannel_) : std::nullopt;
        sample.chipId = chipId_;
        sample.monitorBaseAddress = monitorBaseAddress_;
        sample.ecMmioRegisterValue = ecMmioRegisterValue_;
        sample.fan16BitMode = true;
        sample.diagnostics = diagnostics_;

        if (!initialized_) {
            return sample;
        }

        if (!EnsureDaemonRunning()) {
            sample.diagnostics = diagnostics_;
            return sample;
        }

        PumpAndApplyLatestFrame();

        if (cachedResult_.fields.empty()) {
            sample.diagnostics = diagnostics_;
            return sample;
        }

        sample.chipName = chipName_;
        sample.controllerType = controllerType_;
        sample.selectedFanChannel = selectedChannel_ > 0 ? std::optional<int>(selectedChannel_) : std::nullopt;
        sample.chipId = chipId_;
        sample.monitorBaseAddress = monitorBaseAddress_;
        sample.ecMmioRegisterValue = ecMmioRegisterValue_;
        sample.selectedCpuTemperatureSensor = selectedCpuTemperatureSensor_;
        sample.cpuTemperatureC = selectedCpuTemperatureC_;
        sample.diagnostics = diagnostics_;

        if (!cachedResult_.success) {
            return sample;
        }

        if (!selectedCpuTemperatureC_.has_value()) {
            SelectCpuTemperature();
            sample.selectedCpuTemperatureSensor = selectedCpuTemperatureSensor_;
            sample.cpuTemperatureC = selectedCpuTemperatureC_;
        }

        std::string selectedFanTitle;
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

        if (chosenIndex >= 0 && chosenIndex < static_cast<int>(fanReadings_.size()) &&
            fanReadings_[chosenIndex].rpm.has_value() && *fanReadings_[chosenIndex].rpm > 0.0) {
            selectedChannel_ = chosenIndex + 1;
            sample.fanRpm = fanReadings_[chosenIndex].rpm;
            sample.selectedFanChannel = selectedChannel_;
            selectedFanTitle = fanReadings_[chosenIndex].title;
        }

        sample.available = sample.cpuTemperatureC.has_value() || sample.fanRpm.has_value();
        sample.diagnostics = diagnostics_;
        if (!selectedFanTitle.empty()) {
            sample.diagnostics += " sampled_fan_title=" + selectedFanTitle;
        }
        if (!sample.selectedCpuTemperatureSensor.empty()) {
            sample.diagnostics += " sampled_temp_title=" + sample.selectedCpuTemperatureSensor;
        }
        return sample;
    }

private:
    tracing::Trace& trace() {
        static tracing::Trace nullTrace;
        return trace_ != nullptr ? *trace_ : nullTrace;
    }

    bool EnsureDaemonRunning() {
        DWORD exitCode = STILL_ACTIVE;
        const bool running = daemon_.process != nullptr && GetExitCodeProcess(daemon_.process, &exitCode) && exitCode == STILL_ACTIVE;
        if (running) {
            return true;
        }

        CloseDaemonProcess(daemon_);
        daemonBuffer_.clear();
        daemonFrames_.clear();
        daemonFrameOpen_ = false;
        daemonCurrentFrame_ = ProbeResult{};

        if (!StartProbeDaemon(helperPath_, trace(), daemon_, diagnostics_)) {
            return false;
        }

        const ULONGLONG deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline) {
            if (!PumpProbeDaemon(daemon_, trace(), daemonBuffer_, daemonCurrentFrame_, daemonFrameOpen_, daemonFrames_, diagnostics_)) {
                CloseDaemonProcess(daemon_);
                return false;
            }
            if (!daemonFrames_.empty()) {
                cachedResult_ = daemonFrames_.back();
                daemonFrames_.clear();
                ApplyProbeFields(cachedResult_);
                lastProbeTick_ = GetTickCount64();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        diagnostics_ = "GigabyteFanProbe daemon did not emit an initial frame.";
        CloseDaemonProcess(daemon_);
        return false;
    }

    void PumpAndApplyLatestFrame() {
        if (!PumpProbeDaemon(daemon_, trace(), daemonBuffer_, daemonCurrentFrame_, daemonFrameOpen_, daemonFrames_, diagnostics_)) {
            CloseDaemonProcess(daemon_);
            return;
        }
        if (daemonFrames_.empty()) {
            return;
        }

        cachedResult_ = daemonFrames_.back();
        daemonFrames_.clear();
        ApplyProbeFields(cachedResult_);
        lastProbeTick_ = GetTickCount64();
    }

    void ApplyProbeFields(const ProbeResult& result) {
        diagnostics_ = result.diagnostics.empty() ? "GigabyteFanProbe completed." : result.diagnostics;
        chipName_.clear();
        controllerType_.clear();
        chipId_.reset();
        monitorBaseAddress_.reset();
        ecMmioRegisterValue_.reset();
        fanReadings_.clear();
        tempReadings_.clear();
        selectedCpuTemperatureSensor_.clear();
        selectedCpuTemperatureC_.reset();

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
            } else if (key.rfind("temp_", 0) == 0) {
                const size_t secondUnderscore = key.find('_', 5);
                if (secondUnderscore == std::string::npos) {
                    continue;
                }
                const auto parsedIndex = ParseUnsigned(key.substr(5, secondUnderscore - 5));
                if (!parsedIndex.has_value()) {
                    continue;
                }
                const int index = static_cast<int>(*parsedIndex);
                if (index >= static_cast<int>(tempReadings_.size())) {
                    tempReadings_.resize(index + 1);
                }
                const std::string suffix = key.substr(secondUnderscore + 1);
                if (suffix == "title") {
                    tempReadings_[index].title = value;
                } else if (suffix == "c") {
                    tempReadings_[index].celsius = ParseDouble(value);
                }
            }
        }

        if (chipName_.empty() && !controllerType_.empty()) {
            chipName_ = controllerType_;
        }

        SelectCpuTemperature();
    }

    void SelectCpuTemperature() {
        auto chooseReading = [this](bool requireCpuTitle) -> const TemperatureReading* {
            for (const auto& reading : tempReadings_) {
                if (!reading.celsius.has_value()) {
                    continue;
                }
                if (requireCpuTitle && !ContainsInsensitive(reading.title, "cpu")) {
                    continue;
                }
                return &reading;
            }
            return nullptr;
        };

        const TemperatureReading* selected = chooseReading(true);
        if (selected == nullptr) {
            selected = chooseReading(false);
        }
        if (selected == nullptr) {
            return;
        }

        selectedCpuTemperatureSensor_ = selected->title;
        selectedCpuTemperatureC_ = selected->celsius;
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
    std::vector<TemperatureReading> tempReadings_;
    ProbeResult cachedResult_{};
    ProbeResult daemonCurrentFrame_{};
    DaemonProcess daemon_{};
    std::deque<ProbeResult> daemonFrames_{};
    std::string daemonBuffer_;
    bool daemonFrameOpen_ = false;
    ULONGLONG lastProbeTick_ = 0;
    int selectedChannel_ = 0;
    std::string selectedCpuTemperatureSensor_;
    std::optional<double> selectedCpuTemperatureC_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(tracing::Trace* trace) {
    return std::make_unique<GigabyteSivBoardTelemetryProvider>(trace);
}
