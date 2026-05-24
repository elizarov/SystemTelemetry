#include "telemetry/board/msi/impl/hdi_msi_center.h"

#include <windows.h>

#include <memory>
#include <optional>
#include <winreg.h>

#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"
#include "util/trace.h"

namespace {

constexpr char kMsiUninstallKey[] = "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

std::optional<FilePath> FindInstalledMsiCenterDirectory() {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, kMsiUninstallKey, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD index = 0;
    char childName[256];
    DWORD childNameLength = ARRAYSIZE(childName);
    while (RegEnumKeyExA(uninstallKey, index, childName, &childNameLength, nullptr, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS) {
        HKEY childKey = nullptr;
        if (RegOpenKeyExA(uninstallKey, childName, 0, KEY_READ, &childKey) == ERROR_SUCCESS) {
            const auto displayName = ReadRegistryString(childKey, nullptr, "DisplayName");
            const bool isMsiCenterSdk = displayName.has_value() && ContainsInsensitive(*displayName, "MSI Center SDK");
            if (isMsiCenterSdk) {
                const auto installLocation = ReadRegistryString(childKey, nullptr, "InstallLocation");
                if (installLocation.has_value() && !installLocation->empty()) {
                    const DWORD attributes = GetFileAttributesA(installLocation->c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        RegCloseKey(childKey);
                        RegCloseKey(uninstallKey);
                        return FilePath(*installLocation);
                    }
                }
            }
            RegCloseKey(childKey);
        }
        ++index;
        childNameLength = ARRAYSIZE(childName);
    }

    RegCloseKey(uninstallKey);
    return std::nullopt;
}

class ProductionMsiCenterHdi final : public MsiCenterHdi {
public:
    explicit ProductionMsiCenterHdi(Trace&) {}

    std::optional<FilePath> FindInstalledDirectory() override {
        return FindInstalledMsiCenterDirectory();
    }

    bool Capture(const char* msiCenterDirectory, MsiCenterCaptureSink& sink) override {
        return runtime_.Capture(msiCenterDirectory, sink);
    }

private:
    MsiCenterRuntime runtime_;
};

}  // namespace

std::unique_ptr<MsiCenterHdi> CreateProductionMsiCenterHdi(Trace& trace) {
    return std::make_unique<ProductionMsiCenterHdi>(trace);
}
