#include "telemetry/board/gigabyte/board_gigabyte_siv_bridge.h"

#include <msclr\gcroot.h>
#include <vcclr.h>

#using < mscorlib.dll>
#using < System.dll>

using namespace System;
using namespace System::Collections;
using namespace System::IO;
using namespace System::Reflection;

namespace {

constexpr wchar_t kEngineEnvironmentControlDll[] = L"Gigabyte.Engine.EnvironmentControl.dll";
constexpr wchar_t kEnvironmentControlCommonDll[] = L"Gigabyte.EnvironmentControl.Common.dll";

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
    GigabyteRuntimeContext ^ context, const wchar_t* sivDirectory, GigabyteSivCaptureSink& sink) {
    if (context->loaded) {
        return true;
    }

    context->sivDirectory = gcnew String(sivDirectory);
    context->engineAssemblyPath = CombinePath(context->sivDirectory, kEngineEnvironmentControlDll);
    context->commonAssemblyPath = CombinePath(context->sivDirectory, kEnvironmentControlCommonDll);

    if (!File::Exists(context->engineAssemblyPath)) {
        sink.SetDiagnostics(L"Gigabyte.Engine.EnvironmentControl.dll was not found.");
        return false;
    }
    if (!File::Exists(context->commonAssemblyPath)) {
        sink.SetDiagnostics(L"Gigabyte.EnvironmentControl.Common.dll was not found.");
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
                    pin_ptr<const wchar_t> pinnedFilePath = PtrToStringChars(filePath);
                    sink.TraceAssemblyPreload(pinnedFilePath);
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
                sink.SetDiagnostics(L"Gigabyte hardware-monitor reflection members were not found.");
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

            pin_ptr<const wchar_t> pinnedTypeName = PtrToStringChars(context->monitor->GetType()->FullName);
            sink.TraceMonitorCreated(pinnedTypeName);
            context->initializeMethod->Invoke(context->monitor, gcnew array<Object ^>{context->sourceHwRegister});
            sink.TraceInitializeSuccess();
            context->loaded = true;
            sink.SetDiagnostics(L"Gigabyte SIV hardware-monitor runtime initialized.");
            return true;
        } finally {
            Environment::CurrentDirectory = originalDirectory;
        }
    } catch (Exception ^ ex) {
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        const wchar_t* diagnostics = pinnedDiagnostics;
        sink.SetDiagnostics(diagnostics);
        sink.TraceInitializeException(diagnostics);
        return false;
    }
}

void CollectManagedSensors(
    GigabyteRuntimeContext ^ context, Object ^ sensorKind, bool collectFans, GigabyteSivCaptureSink& sink) {
    array<Object ^> ^ args = collectFans ? context->fanArgs : context->temperatureArgs;
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
        const double numericValue = Convert::ToDouble(valueObject, Globalization::CultureInfo::InvariantCulture);

        String ^ titleText = title != nullptr ? title : String::Empty;
        pin_ptr<const wchar_t> pinnedTitle = PtrToStringChars(titleText);
        if (collectFans) {
            if (!ManagedUnitEquals(unit, context->rpmUnit)) {
                continue;
            }
            sink.AddFanReading(pinnedTitle, numericValue);
        } else {
            if (!ManagedUnitEquals(unit, context->celsiusUnit) && !ManagedUnitEquals(unit, context->degreeCUnit)) {
                continue;
            }
            sink.AddTemperatureReading(pinnedTitle, numericValue);
        }
    }
}

bool CaptureGigabyteSnapshot(
    GigabyteRuntimeContext ^ context, const wchar_t* sivDirectory, GigabyteSivCaptureSink& sink) {
    if (!InitializeGigabyteRuntime(context, sivDirectory, sink)) {
        return false;
    }

    try {
        CollectManagedSensors(context, context->sensorFan, true, sink);
        CollectManagedSensors(context, context->sensorTemperature, false, sink);
        return true;
    } catch (Exception ^ ex) {
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        const wchar_t* diagnostics = pinnedDiagnostics;
        sink.SetDiagnostics(diagnostics);
        sink.TraceSnapshotException(diagnostics);
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

bool GigabyteSivRuntime::Capture(const wchar_t* sivDirectory, GigabyteSivCaptureSink& sink) {
    return CaptureGigabyteSnapshot(impl_->context, sivDirectory, sink);
}
