#include "telemetry/board/lenovo/board_lenovo_vantage_bridge.h"

#include <msclr\gcroot.h>
#include <string_view>
#include <vcclr.h>

#include "util/utf8.h"

#using < mscorlib.dll>
#using < System.dll>
#using < System.Core.dll>
#using < System.Web.Extensions.dll>

using namespace System;
using namespace System::Collections;
using namespace System::Collections::Generic;
using namespace System::IO;
using namespace System::Reflection;
using namespace System::Text;
using namespace System::Threading::Tasks;
using namespace System::Web::Script::Serialization;

namespace {

constexpr char kClientDll[] = "LdeApi.Client.dll";
constexpr char kCoreDll[] = "LdeApi.Core.dll";
constexpr char kRpcClientDll[] = "Lenovo.Vantage.RpcClient.dll";
constexpr char kServerExe[] = "LdeApi.Server.exe";
constexpr int kLoadModulesTimeoutMs = 10000;
constexpr int kExecutionTimeoutMs = 20000;

String ^ ManagedStringFromUtf8(std::string_view text) {
    const std::wstring wide = WideFromUtf8(text);
    return gcnew String(wide.c_str());
}

void SetDiagnosticsUtf8(LenovoHardwareScanCaptureSink& sink, std::string_view text) {
    const std::wstring wide = WideFromUtf8(text);
    sink.SetDiagnostics(wide.c_str());
}

String ^ CombinePath(String ^ directory, const char* fileName) {
    return Path::Combine(directory, ManagedStringFromUtf8(fileName));
}

String ^ ToManagedString(Object ^ value) {
    return value != nullptr ? Convert::ToString(value, Globalization::CultureInfo::InvariantCulture) : String::Empty;
}

double ToDoubleOr(Object ^ value, double defaultValue) {
    if (value == nullptr) {
        return defaultValue;
    }
    try {
        return Convert::ToDouble(value, Globalization::CultureInfo::InvariantCulture);
    } catch (Exception ^) {
        return defaultValue;
    }
}

bool IsSaneCelsius(double value) {
    return value > 0.0 && value <= 125.0;
}

Object ^ GetProperty(Object ^ owner, String ^ name) {
    if (owner == nullptr) {
        return nullptr;
    }
    PropertyInfo ^ property = owner->GetType()->GetProperty(name);
    return property != nullptr ? property->GetValue(owner, nullptr) : nullptr;
}

ref class LenovoAssemblyResolver abstract sealed {
public:
    static void EnsureInstalled(String ^ directory) {
        addinDirectory_ = directory;
        if (installed_) {
            return;
        }
        AppDomain::CurrentDomain->AssemblyResolve +=
            gcnew ResolveEventHandler(&LenovoAssemblyResolver::ResolveAssembly);
        installed_ = true;
    }

private:
    static Assembly ^ ResolveAssembly(Object ^, ResolveEventArgs ^ args) {
        if (String::IsNullOrWhiteSpace(addinDirectory_)) {
            return nullptr;
        }

        AssemblyName ^ name = gcnew AssemblyName(args->Name);
        String ^ dllPath = Path::Combine(addinDirectory_, String::Concat(name->Name, ".dll"));
        if (File::Exists(dllPath)) {
            return Assembly::LoadFrom(dllPath);
        }
        String ^ exePath = Path::Combine(addinDirectory_, String::Concat(name->Name, ".exe"));
        if (File::Exists(exePath)) {
            return Assembly::LoadFrom(exePath);
        }
        return nullptr;
    }

    static String ^ addinDirectory_ = nullptr;
    static bool installed_ = false;
};

ref class LenovoExecutionCapture sealed {
public:
    LenovoExecutionCapture(LenovoHardwareScanCaptureSink& sink, Type ^ resultType)
        : sink_(&sink), serializer_(gcnew JavaScriptSerializer()) {
        Type ^ actionDefinition = Action<Object ^>::typeid->GetGenericTypeDefinition();
        callbackType_ = actionDefinition->MakeGenericType(resultType);
        MethodInfo ^ callbackMethod =
            LenovoExecutionCapture::typeid->GetMethod("OnResult", BindingFlags::NonPublic | BindingFlags::Instance);
        callback_ = Delegate::CreateDelegate(callbackType_, this, callbackMethod);
    }

    property Delegate ^ Callback {
        Delegate ^ get() {
            return callback_;
        }
    }

    property int TemperatureCount {
        int get() {
            return temperatureCount_;
        }
    }

private:
    void OnResult(Object ^ result) {
        if (result == nullptr || sink_ == nullptr) {
            return;
        }

        try {
            String ^ moduleId = ToManagedString(GetProperty(result, "ModuleId"));
            String ^ message = ToManagedString(GetProperty(result, "Message"));
            String ^ traceMessage = message->Replace("\r", " ")->Replace("\n", " ")->Replace("\"", "'");
            String ^ traceText = String::Format(Globalization::CultureInfo::InvariantCulture,
                "execution={0} module={1} device={2} test={3} message_type={4} message=\"{5}\"",
                ToManagedString(GetProperty(result, "Execution")),
                moduleId,
                ToManagedString(GetProperty(result, "DeviceId")),
                ToManagedString(GetProperty(result, "TestId")),
                ToManagedString(GetProperty(result, "MessageType")),
                traceMessage);
            pin_ptr<const wchar_t> pinnedTrace = PtrToStringChars(traceText);
            sink_->TraceExecutionResult(pinnedTrace);

            if (String::IsNullOrWhiteSpace(message)) {
                return;
            }

            Object ^ parsed = serializer_->DeserializeObject(message);
            CollectNode(parsed, moduleId);
        } catch (Exception ^) {
        }
    }

    void CollectNode(Object ^ node, String ^ moduleId) {
        if (node == nullptr) {
            return;
        }

        Dictionary<String ^, Object ^> ^ dictionary = dynamic_cast<Dictionary<String ^, Object ^> ^>(node);
        if (dictionary != nullptr) {
            CollectDictionary(dictionary, moduleId);
            for each (KeyValuePair<String ^, Object ^> pair in dictionary) {
                CollectNode(pair.Value, moduleId);
            }
            return;
        }

        array<Object ^> ^ arrayNode = dynamic_cast<array<Object ^> ^>(node);
        if (arrayNode != nullptr) {
            for each (Object ^ item in arrayNode) {
                CollectNode(item, moduleId);
            }
            return;
        }

        System::Collections::IEnumerable ^ enumerable = dynamic_cast<System::Collections::IEnumerable ^>(node);
        if (enumerable != nullptr && dynamic_cast<String ^>(node) == nullptr) {
            for each (Object ^ item in enumerable) {
                CollectNode(item, moduleId);
            }
        }
    }

    void CollectDictionary(Dictionary<String ^, Object ^> ^ dictionary, String ^ moduleId) {
        Object ^ deviceNameValue = nullptr;
        dictionary->TryGetValue("deviceName", deviceNameValue);
        String ^ deviceName = ToManagedString(deviceNameValue);

        Object ^ temperatureValue = nullptr;
        dictionary->TryGetValue("temperatureCelsius", temperatureValue);
        double celsius = ToDoubleOr(temperatureValue, 0.0);
        if (!IsSaneCelsius(celsius)) {
            celsius = MaximumCoreTemperature(dictionary);
        }
        if (IsSaneCelsius(celsius)) {
            AddTemperature(deviceName, moduleId, celsius);
        }
    }

    double MaximumCoreTemperature(Dictionary<String ^, Object ^> ^ dictionary) {
        Object ^ coresValue = nullptr;
        if (!dictionary->TryGetValue("temperatureCoresCelsius", coresValue) || coresValue == nullptr) {
            return 0.0;
        }

        double maximum = 0.0;
        System::Collections::IEnumerable ^ enumerable = dynamic_cast<System::Collections::IEnumerable ^>(coresValue);
        if (enumerable == nullptr || dynamic_cast<String ^>(coresValue) != nullptr) {
            return 0.0;
        }
        for each (Object ^ item in enumerable) {
            const double value = ToDoubleOr(item, 0.0);
            if (IsSaneCelsius(value) && value > maximum) {
                maximum = value;
            }
        }
        return maximum;
    }

    void AddTemperature(String ^ deviceName, String ^ moduleId, double celsius) {
        String ^ title = NormalizeTemperatureName(deviceName, moduleId);
        pin_ptr<const wchar_t> pinnedTitle = PtrToStringChars(title);
        sink_->AddTemperatureReading(pinnedTitle, celsius);
        ++temperatureCount_;
    }

    static String ^ NormalizeTemperatureName(String ^ deviceName, String ^ moduleId) {
        if (String::Equals(moduleId, "13", StringComparison::Ordinal)) {
            return "CPU Temperature";
        }
        if (String::Equals(moduleId, "2", StringComparison::Ordinal)) {
            return "Motherboard Temperature";
        }
        if (String::Equals(moduleId, "7", StringComparison::Ordinal)) {
            return "GPU Temperature";
        }
        if (String::Equals(moduleId, "1", StringComparison::Ordinal)) {
            return "Disk Temperature";
        }
        if (String::Equals(moduleId, "12", StringComparison::Ordinal)) {
            return "Battery Temperature";
        }
        if (!String::IsNullOrWhiteSpace(deviceName)) {
            return deviceName->IndexOf("temperature", StringComparison::OrdinalIgnoreCase) >= 0
                       ? deviceName
                       : String::Concat(deviceName, " Temperature");
        }
        return "Temperature";
    }

    LenovoHardwareScanCaptureSink* sink_ = nullptr;
    JavaScriptSerializer ^ serializer_ = nullptr;
    Type ^ callbackType_ = nullptr;
    Delegate ^ callback_ = nullptr;
    int temperatureCount_ = 0;
};

ref class LenovoRuntimeContext sealed {
public:
    String ^ addinDirectory = nullptr;
    Assembly ^ coreAssembly = nullptr;
    Assembly ^ clientAssembly = nullptr;
    Type ^ clientType = nullptr;
    Type ^ resultType = nullptr;
    MethodInfo ^ loadModulesMethod = nullptr;
    MethodInfo ^ startExecutionMethod = nullptr;
    MethodInfo ^ getStatusMethod = nullptr;
    MethodInfo ^ disposeMethod = nullptr;
    Object ^ client = nullptr;
    Object ^ loadedModules = nullptr;
    String ^ loadedModuleSignature = nullptr;
    bool loaded = false;
};

Object ^ TaskResult(Task ^ task) {
    PropertyInfo ^ resultProperty = task->GetType()->GetProperty("Result");
    return resultProperty != nullptr ? resultProperty->GetValue(task, nullptr) : nullptr;
}

bool WaitTask(Task ^ task, int timeoutMs, LenovoHardwareScanCaptureSink& sink, const char* timeoutText) {
    if (task == nullptr) {
        SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan returned no task.");
        return false;
    }
    if (!task->Wait(timeoutMs)) {
        SetDiagnosticsUtf8(sink, timeoutText);
        return false;
    }
    if (task->IsFaulted) {
        Exception ^ exception = task->Exception != nullptr ? task->Exception->GetBaseException() : nullptr;
        String ^ text = exception != nullptr ? exception->ToString() : "Lenovo Hardware Scan task faulted.";
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(text);
        sink.SetDiagnostics(pinnedDiagnostics);
        sink.TraceSnapshotException(pinnedDiagnostics);
        return false;
    }
    return true;
}

int EnumerableCount(Object ^ value) {
    System::Collections::IEnumerable ^ enumerable = dynamic_cast<System::Collections::IEnumerable ^>(value);
    if (enumerable == nullptr) {
        return 0;
    }
    int count = 0;
    for each (Object ^ item in enumerable) {
        (void)item;
        ++count;
    }
    return count;
}

array<String ^> ^
    BuildRequestedModules(const LenovoHardwareScanCaptureOptions& options) {
        List<String ^> ^ modules = gcnew List<String ^>();
        if (options.includeCpuTemperature) {
            modules->Add("lde_module_cpu");
        }
        if (options.includeGpuTemperature) {
            modules->Add("lde_module_video_card");
        }
        if (options.includeStorageTemperature) {
            modules->Add("lde_module_storage");
        }
        if (options.includeMotherboardTemperature) {
            modules->Add("lde_module_motherboard");
        }
        if (options.includeBatteryTemperature) {
            modules->Add("lde_module_battery");
        }
        return modules->ToArray();
    }

    String
    ^ ModuleSignature(array<String ^> ^ modules) {
    return modules != nullptr ? String::Join(",", modules) : String::Empty;
}

String ^ ModuleLoadSummary(array<String ^> ^ requestedModules, Object ^ loadedModules) {
    StringBuilder ^ builder = gcnew StringBuilder();
    builder->Append("requested=");
    builder->Append(ModuleSignature(requestedModules));
    builder->Append(" loaded_count=");
    builder->Append(EnumerableCount(loadedModules));

    System::Collections::IEnumerable ^ enumerable = dynamic_cast<System::Collections::IEnumerable ^>(loadedModules);
    if (enumerable == nullptr) {
        return builder->ToString();
    }

    int index = 0;
    for each (Object ^ module in enumerable) {
        builder->Append(" module");
        builder->Append(index);
        builder->Append("=\"");
        builder->Append(ToManagedString(GetProperty(module, "Identifier")));
        String ^ name = ToManagedString(GetProperty(module, "Name"));
        if (!String::IsNullOrWhiteSpace(name)) {
            builder->Append(":");
            builder->Append(name);
        }
        String ^ loadingStatus = ToManagedString(GetProperty(module, "LoadingStatus"));
        if (!String::IsNullOrWhiteSpace(loadingStatus)) {
            builder->Append(":");
            builder->Append(loadingStatus);
        }
        Object ^ devices = GetProperty(module, "Devices");
        builder->Append(":devices=");
        builder->Append(EnumerableCount(devices));
        builder->Append("\"");
        ++index;
    }
    return builder->ToString();
}

bool InitializeLenovoRuntime(
    LenovoRuntimeContext ^ context, const wchar_t* addinDirectory, LenovoHardwareScanCaptureSink& sink) {
    if (context->loaded) {
        return true;
    }

    context->addinDirectory = gcnew String(addinDirectory);
    if (!File::Exists(CombinePath(context->addinDirectory, kClientDll)) ||
        !File::Exists(CombinePath(context->addinDirectory, kCoreDll)) ||
        !File::Exists(CombinePath(context->addinDirectory, kRpcClientDll)) ||
        !File::Exists(CombinePath(context->addinDirectory, kServerExe))) {
        SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan LdeApi files were not found.");
        return false;
    }

    try {
        LenovoAssemblyResolver::EnsureInstalled(context->addinDirectory);
        Environment::SetEnvironmentVariable("PATH",
            String::Concat(context->addinDirectory, ";", Environment::GetEnvironmentVariable("PATH")),
            EnvironmentVariableTarget::Process);

        context->coreAssembly = Assembly::LoadFrom(CombinePath(context->addinDirectory, kCoreDll));
        context->clientAssembly = Assembly::LoadFrom(CombinePath(context->addinDirectory, kClientDll));
        Assembly::LoadFrom(CombinePath(context->addinDirectory, kRpcClientDll));
        context->clientType = context->clientAssembly->GetType("Lenovo.LdeApi.Client.LdeApiClient", true);
        context->resultType = context->coreAssembly->GetType("Lenovo.LdeApi.Core.Models.Result", true);
        Type ^ serverInitModeType = context->coreAssembly->GetType("Lenovo.LdeApi.Core.Enums.ServerInitMode", true);
        Object ^ serverInitMode = Enum::Parse(serverInitModeType, "AsParentProcess", false);

        context->loadModulesMethod = context->clientType->GetMethod("LoadModules");
        context->startExecutionMethod = context->clientType->GetMethod("StartExecution");
        context->getStatusMethod = context->clientType->GetMethod("GetStatus");
        context->disposeMethod = context->clientType->GetMethod("Dispose", Type::EmptyTypes);
        if (context->loadModulesMethod == nullptr || context->startExecutionMethod == nullptr ||
            context->getStatusMethod == nullptr || context->disposeMethod == nullptr) {
            SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan reflection members were not found.");
            return false;
        }

        String ^ logPath = Path::Combine(Path::GetTempPath(), "CaseDashLenovoHardwareScanLogs");
        Directory::CreateDirectory(logPath);
        if (!logPath->EndsWith(Path::DirectorySeparatorChar.ToString())) {
            logPath = String::Concat(logPath, Path::DirectorySeparatorChar);
        }
        String ^ processMonitorName = System::Diagnostics::Process::GetCurrentProcess()->ProcessName;
        context->client = Activator::CreateInstance(
            context->clientType, gcnew array<Object ^>{logPath, nullptr, nullptr, serverInitMode, processMonitorName});

        pin_ptr<const wchar_t> pinnedAssembly = PtrToStringChars(CombinePath(context->addinDirectory, kClientDll));
        sink.TraceAssemblyLoaded(pinnedAssembly);
        context->loaded = true;
        SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan runtime initialized.");
        return true;
    } catch (Exception ^ ex) {
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        sink.SetDiagnostics(pinnedDiagnostics);
        sink.TraceInitializeException(pinnedDiagnostics);
        return false;
    }
}

bool EnsureModulesLoaded(LenovoRuntimeContext ^ context,
    const LenovoHardwareScanCaptureOptions& options,
    LenovoHardwareScanCaptureSink& sink) {
    array<String ^> ^ modules = BuildRequestedModules(options);
    String ^ signature = ModuleSignature(modules);
    if (modules->Length == 0) {
        SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan has no requested modules.");
        return false;
    }

    if (context->loadedModules != nullptr && EnumerableCount(context->loadedModules) > 0 &&
        String::Equals(context->loadedModuleSignature, signature, StringComparison::Ordinal)) {
        return true;
    }

    context->loadedModules = nullptr;
    context->loadedModuleSignature = nullptr;
    Task ^ task =
        safe_cast<Task ^>(context->loadModulesMethod->Invoke(context->client, gcnew array<Object ^>{modules}));
    if (!WaitTask(task, kLoadModulesTimeoutMs, sink, "Lenovo Hardware Scan module load timed out.")) {
        return false;
    }

    context->loadedModules = TaskResult(task);
    context->loadedModuleSignature = signature;
    String ^ loadSummary = ModuleLoadSummary(modules, context->loadedModules);
    pin_ptr<const wchar_t> pinnedLoadSummary = PtrToStringChars(loadSummary);
    sink.TraceModuleLoadResult(pinnedLoadSummary);
    if (EnumerableCount(context->loadedModules) == 0) {
        SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan loaded no thermal modules.");
        return false;
    }
    return true;
}

bool CaptureLenovoSnapshot(LenovoRuntimeContext ^ context,
    const wchar_t* addinDirectory,
    const LenovoHardwareScanCaptureOptions& options,
    LenovoHardwareScanCaptureSink& sink) {
    if (!InitializeLenovoRuntime(context, addinDirectory, sink)) {
        return false;
    }

    try {
        Object ^ status = context->getStatusMethod->Invoke(context->client, gcnew array<Object ^>{});
        String ^ statusText = status != nullptr ? status->ToString() : "null";
        pin_ptr<const wchar_t> pinnedStatus = PtrToStringChars(statusText);
        sink.TraceClientStatus(pinnedStatus);

        if (!EnsureModulesLoaded(context, options, sink)) {
            return false;
        }

        LenovoExecutionCapture ^ capture = gcnew LenovoExecutionCapture(sink, context->resultType);
        Task ^ task = safe_cast<Task ^>(context->startExecutionMethod->Invoke(
            context->client, gcnew array<Object ^>{capture->Callback, context->loadedModules, true, false}));
        if (!WaitTask(task, kExecutionTimeoutMs, sink, "Lenovo Hardware Scan thermal execution timed out.")) {
            return false;
        }

        Object ^ result = TaskResult(task);
        const bool executionStarted = result != nullptr && Convert::ToBoolean(result);
        if (!executionStarted && capture->TemperatureCount == 0) {
            SetDiagnosticsUtf8(sink, "Lenovo Hardware Scan thermal execution returned no telemetry.");
            return false;
        }
        return capture->TemperatureCount > 0;
    } catch (Exception ^ ex) {
        context->loadedModules = nullptr;
        context->loadedModuleSignature = nullptr;
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        sink.SetDiagnostics(pinnedDiagnostics);
        sink.TraceSnapshotException(pinnedDiagnostics);
        return false;
    }
}

void DisposeLenovoRuntime(LenovoRuntimeContext ^ context) {
    if (context == nullptr || context->client == nullptr || context->disposeMethod == nullptr) {
        return;
    }
    try {
        context->disposeMethod->Invoke(context->client, gcnew array<Object ^>{});
    } catch (Exception ^) {
    }
    context->client = nullptr;
    context->loadedModules = nullptr;
    context->loadedModuleSignature = nullptr;
}

}  // namespace

struct LenovoHardwareScanRuntime::Impl {
    Impl() : context(gcnew LenovoRuntimeContext()) {}

    msclr::gcroot<LenovoRuntimeContext ^> context;
};

LenovoHardwareScanRuntime::LenovoHardwareScanRuntime() : impl_(std::make_unique<Impl>()) {}

LenovoHardwareScanRuntime::~LenovoHardwareScanRuntime() {
    DisposeLenovoRuntime(impl_->context);
}

bool LenovoHardwareScanRuntime::Capture(const wchar_t* addinDirectory,
    const LenovoHardwareScanCaptureOptions& options,
    LenovoHardwareScanCaptureSink& sink) {
    return CaptureLenovoSnapshot(impl_->context, addinDirectory, options, sink);
}
