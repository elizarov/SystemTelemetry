#include "telemetry/impl/collector_support.h"

#include <array>
#include <cstdio>
#include <intrin.h>
#include <string_view>

#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"

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
    return CollapseAsciiWhitespace(Trim(brand));
}

}  // namespace

std::string FormatScalarMetric(const ScalarMetric& metric, int precision) {
    if (!metric.value.has_value()) {
        return "N/A";
    }
    char buffer[64];
    const std::string_view unit = EnumToString(metric.unit);
    if (unit.empty()) {
        sprintf_s(buffer, "%.*f", precision, *metric.value);
    } else {
        sprintf_s(buffer, "%.*f %s", precision, *metric.value, std::string(unit).c_str());
    }
    return buffer;
}

std::string PdhStatusCodeString(PDH_STATUS status) {
    char buffer[32];
    sprintf_s(buffer, "%ld", static_cast<long>(status));
    return buffer;
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

std::string DetectCpuName() {
    const std::string cpuidName = DetectCpuNameFromCpuid();
    if (!cpuidName.empty()) {
        return cpuidName;
    }

    const auto registryName = ReadRegistryString(
        HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    if (registryName.has_value()) {
        return CollapseAsciiWhitespace(Trim(*registryName));
    }
    return "";
}
