#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <msclr\marshal_cppstd.h>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vcclr.h>
#include <vector>
#include <winreg.h>

#using < mscorlib.dll>
#using < System.dll>

#include "telemetry/board/board_vendor.h"
#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

using namespace System;
using namespace System::Collections;
using namespace System::IO;
using namespace System::Reflection;
using namespace msclr::interop;

namespace {

constexpr wchar_t kEngineEnvironmentControlDll[] = L"Gigabyte.Engine.EnvironmentControl.dll";
constexpr wchar_t kEnvironmentControlCommonDll[] = L"Gigabyte.EnvironmentControl.Common.dll";
constexpr wchar_t kSivUninstallKey[] = L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr wchar_t kBiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";

struct FanReading {
    std::string title;
    std::optional<double> rpm;
};

struct TemperatureReading {
    std::string title;
    std::optional<double> celsius;
};

struct GigabyteSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<FanReading> fans;
    std::vector<TemperatureReading> temperatures;
};

std::string Utf8FromManagedString(String ^ value) {
    return value == nullptr ? std::string() : marshal_as<std::string>(value);
}

String ^
    ManagedStringFromWide(const std::wstring& value) { return gcnew String(value.c_str()); }

    String
    ^
    ManagedStringFromUtf8(const std::string& value) { return gcnew String(WideFromUtf8(value).c_str()); }

    std::optional<std::wstring> FindInstalledSivDirectory() {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kSivUninstallKey, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD index = 0;
    wchar_t childName[256];
    DWORD childNameLength = ARRAYSIZE(childName);
    while (RegEnumKeyExW(uninstallKey, index, childName, &childNameLength, nullptr, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS) {
        HKEY childKey = nullptr;
        if (RegOpenKeyExW(uninstallKey, childName, 0, KEY_READ, &childKey) == ERROR_SUCCESS) {
            const auto displayName = ReadRegistryWideString(childKey, nullptr, L"DisplayName");
            const bool isSiv =
                displayName.has_value() && (EqualsInsensitive(*displayName, L"SIV") ||
                                               EqualsInsensitive(*displayName, L"System Information Viewer"));
            if (isSiv) {
                const auto installLocation = ReadRegistryWideString(childKey, nullptr, L"InstallLocation");
                if (installLocation.has_value() && !installLocation->empty()) {
                    const DWORD attributes = GetFileAttributesW(installLocation->c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        RegCloseKey(childKey);
                        RegCloseKey(uninstallKey);
                        return installLocation;
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

bool ManagedUnitEquals(String ^ unit, String ^ expected) {
    return String::Equals(unit, expected, StringComparison::OrdinalIgnoreCase);
}

ref class GigabyteAssemblyResolver abstract sealed {
public:
    static void EnsureInstalled(String ^ directory) {
        toolDirectory_ = directory;
        if (installed_) {
            return;
        }
        AppDomain::CurrentDomain->AssemblyResolve +=
            gcnew ResolveEventHandler(&GigabyteAssemblyResolver::ResolveAssembly);
        installed_ = true;
    }

private:
    static Assembly ^
        ResolveAssembly(Object ^, ResolveEventArgs ^ args) {
            if (String::IsNullOrWhiteSpace(toolDirectory_)) {
                return nullptr;
            }

            AssemblyName ^ name = gcnew AssemblyName(args->Name);
            String ^ candidate = Path::Combine(toolDirectory_, name->Name + ".dll");
            if (!File::Exists(candidate)) {
                return nullptr;
            }
            return Assembly::LoadFrom(candidate);
        }

        static String
        ^ toolDirectory_ = nullptr;
    static bool installed_ = false;
};

ref class GigabyteRuntimeContext sealed {
public:
    String ^ sivDirectory = nullptr;
    String ^ engineAssemblyPath = nullptr;
    String ^ commonAssemblyPath = nullptr;
    Assembly ^ engineAssembly = nullptr;
    Assembly ^ commonAssembly = nullptr;
    Type ^ monitorType = nullptr;
    Type ^ sourceType = nullptr;
    Type ^ sensorType = nullptr;
    Type ^ sensorDataType = nullptr;
    Type ^ collectionType = nullptr;
    MethodInfo ^ initializeMethod = nullptr;
    MethodInfo ^ getCurrentMethod = nullptr;
    PropertyInfo ^ titleProperty = nullptr;
    PropertyInfo ^ valueProperty = nullptr;
    PropertyInfo ^ unitProperty = nullptr;
    Object ^ monitor = nullptr;
    Object ^ sourceHwRegister = nullptr;
    Object ^ sensorFan = nullptr;
    Object ^ sensorTemperature = nullptr;
    array<Object ^> ^ fanArgs = nullptr;
    array<Object ^> ^ temperatureArgs = nullptr;
    String ^ rpmUnit = nullptr;
    String ^ celsiusUnit = nullptr;
    String ^ degreeCUnit = nullptr;
    bool loaded = false;
};

bool InitializeGigabyteRuntime(GigabyteRuntimeContext ^ context, tracing::Trace& trace, std::string& diagnostics) {
    if (context->loaded) {
        return true;
    }

    const auto discoveredDirectory = FindInstalledSivDirectory();

    if (!discoveredDirectory.has_value()) {
        diagnostics = "Gigabyte SIV directory was not found in the registry.";
        return false;
    }

    context->sivDirectory = ManagedStringFromWide(*discoveredDirectory);
    context->engineAssemblyPath =
        ManagedStringFromWide((std::filesystem::path(*discoveredDirectory) / kEngineEnvironmentControlDll).wstring());
    context->commonAssemblyPath =
        ManagedStringFromWide((std::filesystem::path(*discoveredDirectory) / kEnvironmentControlCommonDll).wstring());

    if (!File::Exists(context->engineAssemblyPath)) {
        diagnostics = "Gigabyte.Engine.EnvironmentControl.dll was not found.";
        return false;
    }
    if (!File::Exists(context->commonAssemblyPath)) {
        diagnostics = "Gigabyte.EnvironmentControl.Common.dll was not found.";
        return false;
    }

    try {
        String ^ originalDirectory = Environment::CurrentDirectory;
        GigabyteAssemblyResolver::EnsureInstalled(context->sivDirectory);
        try {
            Environment::CurrentDirectory = context->sivDirectory;

            array<String ^> ^ preloadFiles = Directory::GetFiles(context->sivDirectory, "Gigabyte*.dll");
            for each (String ^ filePath in preloadFiles) {
                try {
                    Assembly::LoadFrom(filePath);
                    trace.Write("gigabyte_siv:assembly_preload path=\"" + Utf8FromManagedString(filePath) + "\"");
                } catch (Exception ^) {
                }
            }

            context->engineAssembly = Assembly::LoadFrom(context->engineAssemblyPath);
            context->commonAssembly = Assembly::LoadFrom(context->commonAssemblyPath);
            context->monitorType = context->engineAssembly->GetType(
                "Gigabyte.Engine.EnvironmentControl.HardwareMonitor.HardwareMonitorControlModule", true);
            context->sourceType = context->commonAssembly->GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitorSourceTypes", true);
            context->sensorType = context->commonAssembly->GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.SensorTypes", true);
            context->sensorDataType = context->commonAssembly->GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredData", true);
            context->collectionType = context->commonAssembly->GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredDataCollection", true);
            context->initializeMethod =
                context->monitorType->GetMethod("Initialize", gcnew array<Type ^>{context->sourceType});
            context->getCurrentMethod = context->monitorType->GetMethod("GetCurrentMonitoredData",
                gcnew array<Type ^>{context->sensorType, context->collectionType->MakeByRefType()});
            context->titleProperty = context->sensorDataType->GetProperty("Title");
            context->valueProperty = context->sensorDataType->GetProperty("Value");
            context->unitProperty = context->sensorDataType->GetProperty("Unit");

            if (context->initializeMethod == nullptr || context->getCurrentMethod == nullptr ||
                context->titleProperty == nullptr || context->valueProperty == nullptr ||
                context->unitProperty == nullptr) {
                diagnostics = "Gigabyte hardware-monitor reflection members were not found.";
                return false;
            }

            context->monitor = Activator::CreateInstance(context->monitorType);
            context->sourceHwRegister = Enum::Parse(context->sourceType, "HwRegister", false);
            context->sensorFan = Enum::Parse(context->sensorType, "Fan", false);
            context->sensorTemperature = Enum::Parse(context->sensorType, "Temperature", false);
            context->fanArgs = gcnew array<Object ^>{context->sensorFan, nullptr};
            context->temperatureArgs = gcnew array<Object ^>{context->sensorTemperature, nullptr};
            context->rpmUnit = gcnew String(L"RPM");
            context->celsiusUnit = gcnew String(L"\u2103");
            context->degreeCUnit = gcnew String(L"\u00B0C");

            trace.Write("gigabyte_siv:monitor_created type=\"" +
                        Utf8FromManagedString(context->monitor->GetType()->FullName) + "\"");
            context->initializeMethod->Invoke(context->monitor, gcnew array<Object ^>{context->sourceHwRegister});
            trace.Write("gigabyte_siv:initialize_success source=HwRegister");
            context->loaded = true;
            diagnostics = "Gigabyte SIV hardware-monitor runtime initialized.";
            return true;
        } finally {
            Environment::CurrentDirectory = originalDirectory;
        }
    } catch (Exception ^ ex) {
        diagnostics = Utf8FromManagedString(ex->ToString());
        trace.Write("gigabyte_siv:initialize_exception " + diagnostics);
        return false;
    }
}

void CollectManagedSensors(GigabyteRuntimeContext ^ context,
    Object ^ sensorKind,
    std::vector<FanReading>* fans,
    std::vector<TemperatureReading>* temperatures) {
    array<Object ^> ^ args = fans != nullptr ? context->fanArgs : context->temperatureArgs;
    if (args == nullptr) {
        args = gcnew array<Object ^>{sensorKind, nullptr};
    }
    args[0] = sensorKind;
    // Gigabyte SIV expects a live collection instance here even though the
    // parameter is passed by reference; a null out value faults inside SIV.
    args[1] = Activator::CreateInstance(context->collectionType);
    context->getCurrentMethod->Invoke(context->monitor, args);
    IEnumerable ^ enumerable = dynamic_cast<IEnumerable ^>(args[1]);
    if (enumerable == nullptr) {
        throw gcnew InvalidOperationException("Gigabyte sensor collection did not implement IEnumerable.");
    }

    for each (Object ^ sensor in enumerable) {
        String ^ title = dynamic_cast<String ^>(context->titleProperty->GetValue(sensor, nullptr));
        Object ^ valueObject = context->valueProperty->GetValue(sensor, nullptr);
        String ^ unit = dynamic_cast<String ^>(context->unitProperty->GetValue(sensor, nullptr));
        const std::string titleUtf8 = Utf8FromManagedString(title);
        const double numericValue = Convert::ToDouble(valueObject, Globalization::CultureInfo::InvariantCulture);

        if (fans != nullptr) {
            if (!ManagedUnitEquals(unit, context->rpmUnit)) {
                continue;
            }
            fans->push_back(FanReading{titleUtf8, numericValue});
        } else if (temperatures != nullptr) {
            if (!ManagedUnitEquals(unit, context->celsiusUnit) && !ManagedUnitEquals(unit, context->degreeCUnit)) {
                continue;
            }
            temperatures->push_back(TemperatureReading{titleUtf8, numericValue});
        }
    }
}

bool CaptureGigabyteSnapshot(
    GigabyteRuntimeContext ^ context, GigabyteSnapshot& snapshot, tracing::Trace& trace, std::string& diagnostics) {
    snapshot = GigabyteSnapshot{};

    if (!InitializeGigabyteRuntime(context, trace, diagnostics)) {
        snapshot.diagnostics = diagnostics;
        return false;
    }

    try {
        CollectManagedSensors(context, context->sensorFan, &snapshot.fans, nullptr);
        CollectManagedSensors(context, context->sensorTemperature, nullptr, &snapshot.temperatures);
        snapshot.success = true;

        std::ostringstream details;
        details << "Gigabyte SIV hardware-monitor query completed."
                << " fan_count=" << snapshot.fans.size() << " temp_count=" << snapshot.temperatures.size();
        snapshot.diagnostics = details.str();
        diagnostics = snapshot.diagnostics;
        trace.WriteLazy([&] {
            return "gigabyte_siv:snapshot_done fan_count=" + std::to_string(snapshot.fans.size()) +
                   " temp_count=" + std::to_string(snapshot.temperatures.size());
        });
        return true;
    } catch (Exception ^ ex) {
        diagnostics = Utf8FromManagedString(ex->ToString());
        snapshot.diagnostics = diagnostics;
        trace.WriteLazy([&] { return "gigabyte_siv:snapshot_exception " + diagnostics; });
        return false;
    }
}

std::string ResolveMappedSensorName(
    const std::unordered_map<std::string, std::string>& sensorNames, const std::string& logicalName) {
    const auto it = sensorNames.find(logicalName);
    if (it != sensorNames.end() && !it->second.empty()) {
        return it->second;
    }
    return logicalName;
}

template <typename Reading> std::vector<std::string> ExtractSensorNames(const std::vector<Reading>& readings) {
    std::vector<std::string> names;
    names.reserve(readings.size());
    for (const auto& reading : readings) {
        if (!reading.title.empty()) {
            names.push_back(reading.title);
        }
    }
    return names;
}

void AppendRequestedMetricIndex(
    std::unordered_map<std::string, std::vector<size_t>>& indexBySourceName, std::string sourceName, size_t index) {
    auto& indices = indexBySourceName[std::move(sourceName)];
    if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
        indices.push_back(index);
    }
}

void ResetMetricValues(std::vector<NamedScalarMetric>& metrics) {
    for (auto& metric : metrics) {
        metric.metric.value.reset();
    }
}

class GigabyteSivBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit GigabyteSivBoardTelemetryProvider(tracing::Trace* trace)
        : trace_(trace), runtime_(gcnew GigabyteRuntimeContext()) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        trace().Write("gigabyte_siv:initialize_begin");

        boardManufacturer_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
        boardProduct_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardProduct").value_or("");
        trace().Write(
            "gigabyte_siv:board manufacturer=\"" + boardManufacturer_ + "\" product=\"" + boardProduct_ + "\"");

        if (!ContainsInsensitive(boardManufacturer_, "gigabyte")) {
            diagnostics_ = "Baseboard manufacturer is not Gigabyte.";
            return false;
        }

        const auto sivDirectory = FindInstalledSivDirectory();

        if (!sivDirectory.has_value()) {
            diagnostics_ = "Gigabyte SIV directory was not found in the registry.";
            return false;
        }

        loadedLibrary_ = Utf8FromWide((std::filesystem::path(*sivDirectory) / kEngineEnvironmentControlDll).wstring());
        diagnostics_ = "Gigabyte SIV provider ready.";
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
        requestedTemperatureIndexBySourceName_.clear();
        requestedFanIndexBySourceName_.clear();
        for (size_t i = 0; i < temperatureMetricTemplate_.size(); ++i) {
            AppendRequestedMetricIndex(requestedTemperatureIndexBySourceName_,
                ResolveTemperatureSensorName(temperatureMetricTemplate_[i].name),
                i);
        }
        for (size_t i = 0; i < fanMetricTemplate_.size(); ++i) {
            AppendRequestedMetricIndex(
                requestedFanIndexBySourceName_, ResolveFanSensorName(fanMetricTemplate_[i].name), i);
        }
        requestedDiagnosticsSuffix_.clear();
        if (!settings_.requestedTemperatureNames.empty()) {
            requestedDiagnosticsSuffix_ += " requested_temps=" + JoinNames(settings_.requestedTemperatureNames);
        }
        if (!settings_.requestedFanNames.empty()) {
            requestedDiagnosticsSuffix_ += " requested_fans=" + JoinNames(settings_.requestedFanNames);
        }
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Gigabyte";
        sample.requestedFanNames = settings_.requestedFanNames;
        sample.requestedTemperatureNames = settings_.requestedTemperatureNames;
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = loadedLibrary_;
        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;

        if (!initialized_) {
            return sample;
        }

        GigabyteSnapshot snapshot;
        std::string captureDiagnostics;
        if (!CaptureGigabyteSnapshot(runtime_, snapshot, trace(), captureDiagnostics)) {
            diagnostics_ = captureDiagnostics;
            sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;
            return sample;
        }

        diagnostics_ = snapshot.diagnostics;
        availableFanNames_ = ExtractSensorNames(snapshot.fans);
        availableTemperatureNames_ = ExtractSensorNames(snapshot.temperatures);
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetMetricValues(sample.temperatures);
        ResetMetricValues(sample.fans);
        for (const auto& reading : snapshot.temperatures) {
            const auto it = requestedTemperatureIndexBySourceName_.find(reading.title);
            if (it != requestedTemperatureIndexBySourceName_.end()) {
                for (const size_t index : it->second) {
                    sample.temperatures[index].metric.value = reading.celsius;
                }
            }
        }
        for (const auto& reading : snapshot.fans) {
            const auto it = requestedFanIndexBySourceName_.find(reading.title);
            if (it != requestedFanIndexBySourceName_.end()) {
                for (const size_t index : it->second) {
                    sample.fans[index].metric.value = reading.rpm;
                }
            }
        }
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;
        return sample;
    }

private:
    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        return ResolveMappedSensorName(settings_.temperatureSensorNames, logicalName);
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        return ResolveMappedSensorName(settings_.fanSensorNames, logicalName);
    }

    tracing::Trace& trace() {
        static tracing::Trace nullTrace;
        return trace_ != nullptr ? *trace_ : nullTrace;
    }

    tracing::Trace* trace_ = nullptr;
    BoardTelemetrySettings settings_{};
    gcroot<GigabyteRuntimeContext ^> runtime_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string loadedLibrary_;
    std::string diagnostics_ = "Gigabyte provider not initialized.";
    std::string requestedDiagnosticsSuffix_;
    std::vector<std::string> availableFanNames_;
    std::vector<std::string> availableTemperatureNames_;
    std::vector<NamedScalarMetric> fanMetricTemplate_;
    std::vector<NamedScalarMetric> temperatureMetricTemplate_;
    std::unordered_map<std::string, std::vector<size_t>> requestedFanIndexBySourceName_;
    std::unordered_map<std::string, std::vector<size_t>> requestedTemperatureIndexBySourceName_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(tracing::Trace* trace) {
    return std::make_unique<GigabyteSivBoardTelemetryProvider>(trace);
}
