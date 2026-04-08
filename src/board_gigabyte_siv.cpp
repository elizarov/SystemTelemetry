#define NOMINMAX
#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <vcclr.h>
#include <msclr\marshal_cppstd.h>

#using <mscorlib.dll>
#using <System.dll>

#include "board_vendor.h"
#include "trace.h"
#include "utf8.h"

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

bool EqualsInsensitive(const std::string& left, const std::string& right) {
    return ToLower(left) == ToLower(right);
}

bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (::towlower(left[i]) != ::towlower(right[i])) {
            return false;
        }
    }
    return true;
}

template <typename Reading>
const Reading* FindReadingByName(const std::vector<Reading>& readings, const std::string& requestedName) {
    if (requestedName.empty()) {
        return nullptr;
    }

    for (const auto& reading : readings) {
        if (EqualsInsensitive(reading.title, requestedName)) {
            return &reading;
        }
    }
    return nullptr;
}

std::wstring CombinePath(const std::wstring& directory, const wchar_t* name) {
    std::wstring path = directory;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    path += name;
    return path;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            joined += ",";
        }
        joined += names[i];
    }
    return joined;
}

std::string Utf8FromManagedString(String^ value) {
    return value == nullptr ? std::string() : marshal_as<std::string>(value);
}

String^ ManagedStringFromWide(const std::wstring& value) {
    return gcnew String(value.c_str());
}

String^ ManagedStringFromUtf8(const std::string& value) {
    return gcnew String(WideFromUtf8(value).c_str());
}

std::optional<std::wstring> FindInstalledSivDirectory() {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kSivUninstallKey, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD index = 0;
    wchar_t childName[256];
    DWORD childNameLength = ARRAYSIZE(childName);
    while (RegEnumKeyExW(uninstallKey, index, childName, &childNameLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY childKey = nullptr;
        if (RegOpenKeyExW(uninstallKey, childName, 0, KEY_READ, &childKey) == ERROR_SUCCESS) {
            const auto displayName = ReadRegistryWideString(childKey, nullptr, L"DisplayName");
            const bool isSiv = displayName.has_value() &&
                (EqualsInsensitive(*displayName, L"SIV") || EqualsInsensitive(*displayName, L"System Information Viewer"));
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

bool ManagedUnitEquals(String^ unit, String^ expected) {
    return String::Equals(unit, expected, StringComparison::OrdinalIgnoreCase);
}

ref class GigabyteAssemblyResolver abstract sealed {
public:
    static void EnsureInstalled(String^ directory) {
        toolDirectory_ = directory;
        if (installed_) {
            return;
        }
        AppDomain::CurrentDomain->AssemblyResolve += gcnew ResolveEventHandler(&GigabyteAssemblyResolver::ResolveAssembly);
        installed_ = true;
    }

private:
    static Assembly^ ResolveAssembly(Object^, ResolveEventArgs^ args) {
        if (String::IsNullOrWhiteSpace(toolDirectory_)) {
            return nullptr;
        }

        AssemblyName^ name = gcnew AssemblyName(args->Name);
        String^ candidate = Path::Combine(toolDirectory_, name->Name + ".dll");
        if (!File::Exists(candidate)) {
            return nullptr;
        }
        return Assembly::LoadFrom(candidate);
    }

    static String^ toolDirectory_ = nullptr;
    static bool installed_ = false;
};

ref class GigabyteRuntimeContext sealed {
public:
    String^ sivDirectory = nullptr;
    String^ engineAssemblyPath = nullptr;
    String^ commonAssemblyPath = nullptr;
    Assembly^ engineAssembly = nullptr;
    Assembly^ commonAssembly = nullptr;
    Type^ monitorType = nullptr;
    Type^ sourceType = nullptr;
    Type^ sensorType = nullptr;
    Type^ sensorDataType = nullptr;
    Type^ collectionType = nullptr;
    MethodInfo^ initializeMethod = nullptr;
    MethodInfo^ getCurrentMethod = nullptr;
    PropertyInfo^ titleProperty = nullptr;
    PropertyInfo^ valueProperty = nullptr;
    PropertyInfo^ unitProperty = nullptr;
    Object^ monitor = nullptr;
    Object^ sourceHwRegister = nullptr;
    Object^ sensorFan = nullptr;
    Object^ sensorTemperature = nullptr;
    bool loaded = false;
};

bool InitializeGigabyteRuntime(GigabyteRuntimeContext^ context, tracing::Trace& trace, std::string& diagnostics) {
    if (context->loaded) {
        return true;
    }

    const auto discoveredDirectory = FindInstalledSivDirectory();

    if (!discoveredDirectory.has_value()) {
        diagnostics = "Gigabyte SIV directory was not found in the registry.";
        return false;
    }

    context->sivDirectory = ManagedStringFromWide(*discoveredDirectory);
    context->engineAssemblyPath = ManagedStringFromWide(CombinePath(*discoveredDirectory, kEngineEnvironmentControlDll));
    context->commonAssemblyPath = ManagedStringFromWide(CombinePath(*discoveredDirectory, kEnvironmentControlCommonDll));

    if (!File::Exists(context->engineAssemblyPath)) {
        diagnostics = "Gigabyte.Engine.EnvironmentControl.dll was not found.";
        return false;
    }
    if (!File::Exists(context->commonAssemblyPath)) {
        diagnostics = "Gigabyte.EnvironmentControl.Common.dll was not found.";
        return false;
    }

    try {
        Environment::CurrentDirectory = context->sivDirectory;
        GigabyteAssemblyResolver::EnsureInstalled(context->sivDirectory);

        array<String^>^ preloadFiles = Directory::GetFiles(context->sivDirectory, "Gigabyte*.dll");
        for each (String ^ filePath in preloadFiles) {
            try {
                Assembly::LoadFrom(filePath);
                trace.Write("gigabyte_siv:assembly_preload path=\"" + Utf8FromManagedString(filePath) + "\"");
            } catch (Exception^) {
            }
        }

        context->engineAssembly = Assembly::LoadFrom(context->engineAssemblyPath);
        context->commonAssembly = Assembly::LoadFrom(context->commonAssemblyPath);
        context->monitorType = context->engineAssembly->GetType("Gigabyte.Engine.EnvironmentControl.HardwareMonitor.HardwareMonitorControlModule", true);
        context->sourceType = context->commonAssembly->GetType("Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitorSourceTypes", true);
        context->sensorType = context->commonAssembly->GetType("Gigabyte.EnvironmentControl.Common.HardwareMonitor.SensorTypes", true);
        context->sensorDataType = context->commonAssembly->GetType("Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredData", true);
        context->collectionType = context->commonAssembly->GetType("Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredDataCollection", true);
        context->initializeMethod = context->monitorType->GetMethod("Initialize", gcnew array<Type^>{ context->sourceType });
        context->getCurrentMethod = context->monitorType->GetMethod("GetCurrentMonitoredData", gcnew array<Type^>{ context->sensorType, context->collectionType->MakeByRefType() });
        context->titleProperty = context->sensorDataType->GetProperty("Title");
        context->valueProperty = context->sensorDataType->GetProperty("Value");
        context->unitProperty = context->sensorDataType->GetProperty("Unit");

        if (context->initializeMethod == nullptr || context->getCurrentMethod == nullptr ||
            context->titleProperty == nullptr || context->valueProperty == nullptr || context->unitProperty == nullptr) {
            diagnostics = "Gigabyte hardware-monitor reflection members were not found.";
            return false;
        }

        context->monitor = Activator::CreateInstance(context->monitorType);
        context->sourceHwRegister = Enum::Parse(context->sourceType, "HwRegister", false);
        context->sensorFan = Enum::Parse(context->sensorType, "Fan", false);
        context->sensorTemperature = Enum::Parse(context->sensorType, "Temperature", false);

        trace.Write("gigabyte_siv:monitor_created type=\"" + Utf8FromManagedString(context->monitor->GetType()->FullName) + "\"");
        context->initializeMethod->Invoke(context->monitor, gcnew array<Object^>{ context->sourceHwRegister });
        trace.Write("gigabyte_siv:initialize_success source=HwRegister");
        context->loaded = true;
        diagnostics = "Gigabyte SIV hardware-monitor runtime initialized.";
        return true;
    } catch (Exception^ ex) {
        diagnostics = Utf8FromManagedString(ex->ToString());
        trace.Write("gigabyte_siv:initialize_exception " + diagnostics);
        return false;
    }
}


void CollectManagedSensors(GigabyteRuntimeContext^ context, Object^ sensorKind,
    std::vector<FanReading>* fans, std::vector<TemperatureReading>* temperatures) {
    Object^ collection = Activator::CreateInstance(context->collectionType);
    array<Object^>^ args = gcnew array<Object^>{ sensorKind, collection };
    context->getCurrentMethod->Invoke(context->monitor, args);
    IEnumerable^ enumerable = dynamic_cast<IEnumerable^>(args[1]);
    if (enumerable == nullptr) {
        throw gcnew InvalidOperationException("Gigabyte sensor collection did not implement IEnumerable.");
    }

    for each (Object ^ sensor in enumerable) {
        String^ title = dynamic_cast<String^>(context->titleProperty->GetValue(sensor, nullptr));
        Object^ valueObject = context->valueProperty->GetValue(sensor, nullptr);
        String^ unit = dynamic_cast<String^>(context->unitProperty->GetValue(sensor, nullptr));
        const std::string titleUtf8 = Utf8FromManagedString(title);
        const double numericValue = Convert::ToDouble(valueObject, Globalization::CultureInfo::InvariantCulture);

        if (fans != nullptr) {
            if (!ManagedUnitEquals(unit, "RPM")) {
                continue;
            }
            fans->push_back(FanReading{ titleUtf8, numericValue });
        } else if (temperatures != nullptr) {
            if (!ManagedUnitEquals(unit, gcnew String(L"\u2103")) && !ManagedUnitEquals(unit, gcnew String(L"\u00B0C"))) {
                continue;
            }
            temperatures->push_back(TemperatureReading{ titleUtf8, numericValue });
        }
    }
}

bool CaptureGigabyteSnapshot(GigabyteRuntimeContext^ context, GigabyteSnapshot& snapshot,
    tracing::Trace& trace, std::string& diagnostics) {
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
                << " fan_count=" << snapshot.fans.size()
                << " temp_count=" << snapshot.temperatures.size();
        snapshot.diagnostics = details.str();
        diagnostics = snapshot.diagnostics;
        trace.Write("gigabyte_siv:snapshot_done fan_count=" + std::to_string(snapshot.fans.size()) + " temp_count=" + std::to_string(snapshot.temperatures.size()));
        return true;
    } catch (Exception^ ex) {
        diagnostics = Utf8FromManagedString(ex->ToString());
        snapshot.diagnostics = diagnostics;
        trace.Write("gigabyte_siv:snapshot_exception " + diagnostics);
        return false;
    }
}

class GigabyteSivBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit GigabyteSivBoardTelemetryProvider(tracing::Trace* trace) : trace_(trace), runtime_(gcnew GigabyteRuntimeContext()) {}

    bool Initialize(const AppConfig& config) override {
        config_ = config;
        trace().Write("gigabyte_siv:initialize_begin");

        boardManufacturer_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
        boardProduct_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardProduct").value_or("");
        trace().Write("gigabyte_siv:board manufacturer=\"" + boardManufacturer_ + "\" product=\"" + boardProduct_ + "\"");

        if (!ContainsInsensitive(boardManufacturer_, "gigabyte")) {
            diagnostics_ = "Baseboard manufacturer is not Gigabyte.";
            return false;
        }

        const auto sivDirectory = FindInstalledSivDirectory();

        if (!sivDirectory.has_value()) {
            diagnostics_ = "Gigabyte SIV directory was not found in the registry.";
            return false;
        }

        loadedLibrary_ = Utf8FromWide(CombinePath(*sivDirectory, kEngineEnvironmentControlDll));
        diagnostics_ = "Gigabyte SIV provider ready.";
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Gigabyte";
        sample.requestedFanNames = config_.board.requestedFanNames;
        sample.requestedTemperatureNames = config_.board.requestedTemperatureNames;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = loadedLibrary_;
        sample.temperatures = BuildRequestedTemperatures();
        sample.fans = BuildRequestedFans();
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_;

        if (!initialized_) {
            return sample;
        }

        GigabyteSnapshot snapshot;
        std::string captureDiagnostics;
        if (!CaptureGigabyteSnapshot(runtime_, snapshot, trace(), captureDiagnostics)) {
            diagnostics_ = captureDiagnostics;
            sample.diagnostics = diagnostics_;
            return sample;
        }

        diagnostics_ = snapshot.diagnostics;
        fanReadings_ = std::move(snapshot.fans);
        tempReadings_ = std::move(snapshot.temperatures);

        sample.temperatures = BuildRequestedTemperatures();
        sample.fans = BuildRequestedFans();
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_;
        if (!sample.requestedTemperatureNames.empty()) {
            sample.diagnostics += " requested_temps=" + JoinNames(sample.requestedTemperatureNames);
        }
        if (!sample.requestedFanNames.empty()) {
            sample.diagnostics += " requested_fans=" + JoinNames(sample.requestedFanNames);
        }
        return sample;
    }

private:
    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        const auto it = config_.board.temperatureSensorNames.find(logicalName);
        if (it != config_.board.temperatureSensorNames.end() && !it->second.empty()) {
            return it->second;
        }
        return logicalName;
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        const auto it = config_.board.fanSensorNames.find(logicalName);
        if (it != config_.board.fanSensorNames.end() && !it->second.empty()) {
            return it->second;
        }
        return logicalName;
    }

    tracing::Trace& trace() {
        static tracing::Trace nullTrace;
        return trace_ != nullptr ? *trace_ : nullTrace;
    }

    static bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics) {
        for (const auto& metric : metrics) {
            if (metric.metric.value.has_value()) {
                return true;
            }
        }
        return false;
    }

    std::vector<NamedScalarMetric> BuildRequestedTemperatures() const {
        std::vector<NamedScalarMetric> metrics;
        metrics.reserve(config_.board.requestedTemperatureNames.size());
        for (const auto& requestedName : config_.board.requestedTemperatureNames) {
            NamedScalarMetric metric;
            metric.name = requestedName;
            metric.metric.unit = "\xC2\xB0""C";
            if (const TemperatureReading* reading =
                    FindReadingByName(tempReadings_, ResolveTemperatureSensorName(requestedName));
                reading != nullptr) {
                metric.metric.value = reading->celsius;
            }
            metrics.push_back(std::move(metric));
        }
        return metrics;
    }

    std::vector<NamedScalarMetric> BuildRequestedFans() const {
        std::vector<NamedScalarMetric> metrics;
        metrics.reserve(config_.board.requestedFanNames.size());
        for (const auto& requestedName : config_.board.requestedFanNames) {
            NamedScalarMetric metric;
            metric.name = requestedName;
            metric.metric.unit = "RPM";
            if (const FanReading* reading = FindReadingByName(fanReadings_, ResolveFanSensorName(requestedName));
                reading != nullptr) {
                metric.metric.value = reading->rpm;
            }
            metrics.push_back(std::move(metric));
        }
        return metrics;
    }

    tracing::Trace* trace_ = nullptr;
    AppConfig config_{};
    gcroot<GigabyteRuntimeContext^> runtime_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string loadedLibrary_;
    std::string diagnostics_ = "Gigabyte provider not initialized.";
    std::vector<FanReading> fanReadings_;
    std::vector<TemperatureReading> tempReadings_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(tracing::Trace* trace) {
    return std::make_unique<GigabyteSivBoardTelemetryProvider>(trace);
}




