#include "telemetry/board/gigabyte/board_gigabyte_siv_bridge.h"

#include <msclr\gcroot.h>
#include <msclr\marshal_cppstd.h>

#using < mscorlib.dll>
#using < System.dll>

#include "util/trace.h"

using namespace System;
using namespace System::Collections;
using namespace System::IO;
using namespace System::Reflection;
using namespace msclr::interop;

namespace {

constexpr wchar_t kEngineEnvironmentControlDll[] = L"Gigabyte.Engine.EnvironmentControl.dll";
constexpr wchar_t kEnvironmentControlCommonDll[] = L"Gigabyte.EnvironmentControl.Common.dll";

std::string Utf8FromManagedString(String ^ value) {
    return value == nullptr ? std::string() : marshal_as<std::string>(value);
}

String ^ ManagedStringFromWide(const std::wstring& value) { return gcnew String(value.c_str()); }

    bool ManagedUnitEquals(String ^ unit, String ^ expected) {
    return String::Equals(unit, expected, StringComparison::OrdinalIgnoreCase);
}

String ^ CombinePath(
             String ^ directory, const wchar_t* fileName) { return Path::Combine(directory, gcnew String(fileName)); }

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

bool InitializeGigabyteRuntime(
    GigabyteRuntimeContext ^ context, const std::wstring& sivDirectory, Trace& trace, std::string& diagnostics) {
    if (context->loaded) {
        return true;
    }

    context->sivDirectory = ManagedStringFromWide(sivDirectory);
    context->engineAssemblyPath = CombinePath(context->sivDirectory, kEngineEnvironmentControlDll);
    context->commonAssemblyPath = CombinePath(context->sivDirectory, kEnvironmentControlCommonDll);

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
    std::vector<GigabyteSivFanReading>* fans,
    std::vector<GigabyteSivTemperatureReading>* temperatures) {
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
            fans->push_back(GigabyteSivFanReading{titleUtf8, numericValue});
        } else if (temperatures != nullptr) {
            if (!ManagedUnitEquals(unit, context->celsiusUnit) && !ManagedUnitEquals(unit, context->degreeCUnit)) {
                continue;
            }
            temperatures->push_back(GigabyteSivTemperatureReading{titleUtf8, numericValue});
        }
    }
}

bool CaptureGigabyteSnapshot(GigabyteRuntimeContext ^ context,
    const std::wstring& sivDirectory,
    GigabyteSivSnapshot& snapshot,
    Trace& trace,
    std::string& diagnostics) {
    snapshot = GigabyteSivSnapshot{};

    if (!InitializeGigabyteRuntime(context, sivDirectory, trace, diagnostics)) {
        snapshot.diagnostics = diagnostics;
        return false;
    }

    try {
        CollectManagedSensors(context, context->sensorFan, &snapshot.fans, nullptr);
        CollectManagedSensors(context, context->sensorTemperature, nullptr, &snapshot.temperatures);
        snapshot.success = true;

        snapshot.diagnostics =
            "Gigabyte SIV hardware-monitor query completed. fan_count=" + std::to_string(snapshot.fans.size()) +
            " temp_count=" + std::to_string(snapshot.temperatures.size());
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

}  // namespace

struct GigabyteSivRuntime::Impl {
    Impl() : context(gcnew GigabyteRuntimeContext()) {}

    msclr::gcroot<GigabyteRuntimeContext ^> context;
};

GigabyteSivRuntime::GigabyteSivRuntime() : impl_(std::make_unique<Impl>()) {}

GigabyteSivRuntime::~GigabyteSivRuntime() = default;

bool GigabyteSivRuntime::Capture(
    const std::wstring& sivDirectory, GigabyteSivSnapshot& snapshot, Trace& trace, std::string& diagnostics) {
    return CaptureGigabyteSnapshot(impl_->context, sivDirectory, snapshot, trace, diagnostics);
}
