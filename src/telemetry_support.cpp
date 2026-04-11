#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <pdh.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "telemetry_support.h"
#include "utf8.h"

std::string NormalizeDriveLetter(const std::string& drive) {
    if (drive.empty()) {
        return {};
    }
    const unsigned char ch = static_cast<unsigned char>(drive.front());
    if (!std::isalpha(ch)) {
        return {};
    }
    return std::string(1, static_cast<char>(std::toupper(ch)));
}

bool IsSelectableStorageDriveType(UINT driveType) {
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE;
}

std::vector<StorageDriveCandidate> EnumerateStorageDriveCandidates(const std::vector<std::string>& selectedDrives) {
    std::vector<std::string> normalizedSelected;
    normalizedSelected.reserve(selectedDrives.size());
    for (const auto& drive : selectedDrives) {
        const std::string letter = NormalizeDriveLetter(drive);
        if (letter.empty()) {
            continue;
        }
        if (std::find(normalizedSelected.begin(), normalizedSelected.end(), letter) == normalizedSelected.end()) {
            normalizedSelected.push_back(letter);
        }
    }

    std::vector<StorageDriveCandidate> candidates;
    const DWORD bufferLength = GetLogicalDriveStringsW(0, nullptr);
    if (bufferLength == 0) {
        return candidates;
    }

    std::vector<wchar_t> buffer(bufferLength + 1, L'\0');
    const DWORD copied = GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (copied == 0 || copied >= buffer.size()) {
        return {};
    }

    for (const wchar_t* current = buffer.data(); *current != L'\0'; current += wcslen(current) + 1) {
        const std::wstring root(current);
        if (root.size() < 2 || !iswalpha(root[0])) {
            continue;
        }

        const UINT driveType = GetDriveTypeW(root.c_str());
        if (!IsSelectableStorageDriveType(driveType)) {
            continue;
        }

        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root.c_str(), nullptr, &totalBytes, nullptr) || totalBytes.QuadPart == 0) {
            continue;
        }

        StorageDriveCandidate candidate;
        candidate.letter = std::string(1, static_cast<char>(towupper(root[0])));
        candidate.volumeLabel = ReadVolumeLabel(root);
        candidate.totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        candidate.driveType = driveType;
        candidate.selected =
            std::find(normalizedSelected.begin(), normalizedSelected.end(), candidate.letter) != normalizedSelected.end();
        candidates.push_back(std::move(candidate));
    }

    std::sort(candidates.begin(),
        candidates.end(),
        [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) { return lhs.letter < rhs.letter; });
    return candidates;
}

std::string ReadVolumeLabel(const std::wstring& root) {
    wchar_t volumeName[MAX_PATH] = {};
    if (!GetVolumeInformationW(
            root.c_str(), volumeName, ARRAYSIZE(volumeName), nullptr, nullptr, nullptr, nullptr, 0)) {
        return {};
    }
    return Utf8FromWide(volumeName);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string FormatScalarMetric(const ScalarMetric& metric, int precision) {
    if (!metric.value.has_value()) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.*f %s", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(const std::vector<std::string>& names, const char* unit) {
    std::vector<NamedScalarMetric> metrics;
    metrics.reserve(names.size());
    for (const auto& name : names) {
        metrics.push_back(NamedScalarMetric{name, ScalarMetric{std::nullopt, unit}});
    }
    return metrics;
}

bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics) {
    for (const auto& metric : metrics) {
        if (metric.metric.value.has_value()) {
            return true;
        }
    }
    return false;
}

typedef PDH_STATUS(WINAPI* PdhAddEnglishCounterWFn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);

PDH_STATUS AddCounterCompat(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter) {
    static PdhAddEnglishCounterWFn addEnglish = reinterpret_cast<PdhAddEnglishCounterWFn>(
        GetProcAddress(GetModuleHandleW(L"pdh.dll"), "PdhAddEnglishCounterW"));
    if (addEnglish != nullptr) {
        return addEnglish(query, path, 0, counter);
    }
    return PdhAddCounterW(query, path, 0, counter);
}

bool ContainsInsensitive(const std::wstring& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLowerAscii(Utf8FromWide(value)).find(ToLowerAscii(needle)) != std::string::npos;
}

bool EqualsInsensitive(const std::wstring& value, const std::string& needle) {
    if (needle.empty()) {
        return false;
    }
    return ToLowerAscii(Utf8FromWide(value)) == ToLowerAscii(needle);
}

std::string TrimAsciiWhitespace(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };

    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    if (begin == value.end()) {
        return "";
    }

    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(begin, end);
}

std::string CollapseAsciiWhitespace(std::string value) {
    std::string collapsed;
    collapsed.reserve(value.size());

    bool pendingSpace = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            pendingSpace = !collapsed.empty();
            continue;
        }
        if (pendingSpace) {
            collapsed.push_back(' ');
            pendingSpace = false;
        }
        collapsed.push_back(static_cast<char>(ch));
    }
    return collapsed;
}

std::optional<std::wstring> ReadRegistryWideString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
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
    return value;
}

std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    const auto value = ReadRegistryWideString(root, subKey, valueName);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return Utf8FromWide(*value);
}

namespace {

std::string DetectCpuNameFromCpuid() {
    int maxExtendedLeaf[4]{};
    __cpuid(maxExtendedLeaf, 0x80000000);
    if (static_cast<unsigned int>(maxExtendedLeaf[0]) < 0x80000004) {
        return "";
    }

    std::array<int, 12> brandWords{};
    for (int i = 0; i < 3; ++i) {
        int leafData[4]{};
        __cpuid(leafData, 0x80000002 + i);
        for (int j = 0; j < 4; ++j) {
            brandWords[static_cast<size_t>(i) * 4 + static_cast<size_t>(j)] = leafData[j];
        }
    }

    std::string brand(reinterpret_cast<const char*>(brandWords.data()), brandWords.size() * sizeof(int));
    const size_t terminator = brand.find('\0');
    if (terminator != std::string::npos) {
        brand.resize(terminator);
    }
    return CollapseAsciiWhitespace(TrimAsciiWhitespace(brand));
}

}  // namespace

std::string DetectCpuName() {
    const std::string cpuidName = DetectCpuNameFromCpuid();
    if (!cpuidName.empty()) {
        return cpuidName;
    }

    const auto registryName = ReadRegistryString(
        HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    if (registryName.has_value()) {
        return CollapseAsciiWhitespace(TrimAsciiWhitespace(*registryName));
    }
    return "";
}
