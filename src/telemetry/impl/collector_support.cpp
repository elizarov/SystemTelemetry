#include "telemetry/impl/collector_support.h"

#include <array>
#include <intrin.h>
#include <string>
#include <string_view>

#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"
#include "util/text_format.h"

namespace {

constexpr char kPdhLibraryName[] = "pdh.dll";

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
    const std::string_view unit = EnumToString(metric.unit);
    if (unit.empty()) {
        return FormatText("%.*f", precision, *metric.value);
    }
    return FormatText("%.*f %.*s", precision, *metric.value, static_cast<int>(unit.size()), unit.data());
}

typedef PDH_STATUS(WINAPI* PdhAddEnglishCounterAFn)(PDH_HQUERY, LPCSTR, DWORD_PTR, PDH_HCOUNTER*);

PDH_STATUS AddCounterCompat(PDH_HQUERY query, std::string_view path, PDH_HCOUNTER* counter) {
    const std::string pathText(path);
    static PdhAddEnglishCounterAFn addEnglish = reinterpret_cast<PdhAddEnglishCounterAFn>(
        GetProcAddress(GetModuleHandleA(kPdhLibraryName), "PdhAddEnglishCounterA"));
    if (addEnglish != nullptr) {
        return addEnglish(query, pathText.c_str(), 0, counter);
    }
    return PdhAddCounterA(query, pathText.c_str(), 0, counter);
}

std::string DetectCpuName() {
    const std::string cpuidName = DetectCpuNameFromCpuid();
    if (!cpuidName.empty()) {
        return cpuidName;
    }

    const auto registryName = ReadRegistryString(
        HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString");
    if (registryName.has_value()) {
        return CollapseAsciiWhitespace(Trim(*registryName));
    }
    return "";
}
