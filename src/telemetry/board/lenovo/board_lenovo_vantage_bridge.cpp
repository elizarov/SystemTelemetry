#include "telemetry/board/lenovo/board_lenovo_vantage_bridge.h"

#include <msclr\gcroot.h>
#include <string_view>
#include <vcclr.h>

#include "util/text_encoding.h"

#using < mscorlib.dll>
#using < System.dll>
#using < System.Core.dll>
#using < System.Web.Extensions.dll>

using namespace System;
using namespace System::Collections;
using namespace System::Collections::Generic;
using namespace System::Diagnostics;
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

String ^ ManagedStringFromText(std::string_view text) {
    const std::wstring wide = WideFromText(text);
    return gcnew String(wide.c_str());
}

void SetDiagnosticsText(LenovoHardwareScanCaptureSink& sink, std::string_view text) {
    const std::wstring wide = WideFromText(text);
    sink.SetDiagnostics(wide.c_str());
}

Stopwatch ^ StartTiming(LenovoHardwareScanCaptureSink& sink) {
    return sink.TraceTimingEnabled() ? Stopwatch::StartNew() : nullptr;
}

void TraceTiming(LenovoHardwareScanCaptureSink& sink, const char* phase, Stopwatch ^ stopwatch) {
    if (stopwatch == nullptr) {
        return;
    }
    String ^ timing = String::Format(Globalization::CultureInfo::InvariantCulture,
        "phase={0} elapsed_ms={1:F2}",
        ManagedStringFromText(phase),
        stopwatch->Elapsed.TotalMilliseconds);
    pin_ptr<const wchar_t> pinnedTiming = PtrToStringChars(timing);
    sink.TraceTiming(pinnedTiming);
}

String ^ CombinePath(String ^ directory, const char* fileName) {
    return Path::Combine(directory, ManagedStringFromText(fileName));
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
    MethodInfo ^ getAvailableModulesMethod = nullptr;
    MethodInfo ^ startExecutionMethod = nullptr;
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
        SetDiagnosticsText(sink, "Lenovo Hardware Scan returned no task.");
        return false;
    }
    if (!task->Wait(timeoutMs)) {
        SetDiagnosticsText(sink, timeoutText);
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

Object ^ CreateGenericList(Type ^ itemType) {
    Type ^ listDefinition = List<Object ^>::typeid->GetGenericTypeDefinition();
    return Activator::CreateInstance(listDefinition->MakeGenericType(itemType));
}

void AddToList(Object ^ list, Object ^ value) {
    System::Collections::IList ^ items = dynamic_cast<System::Collections::IList ^>(list);
    if (items != nullptr) {
        items->Add(value);
    }
}

Object ^ BuildManualCpuThermalToolModules(Assembly ^ coreAssembly) {
    Type ^ toolType = coreAssembly->GetType("Lenovo.LdeApi.Core.Models.Tool", true);
    Type ^ deviceType = coreAssembly->GetType("Lenovo.LdeApi.Core.Models.Device", true);
    Type ^ moduleType = coreAssembly->GetType("Lenovo.LdeApi.Core.Models.Module", true);
    Type ^ parameterType = coreAssembly->GetType("Lenovo.LdeApi.Core.Models.Parameter", true);
    Type ^ loadingStatusType = coreAssembly->GetType("Lenovo.LdeApi.Core.Enums.ModuleLoadingStatus", true);

    Object ^ parameters = CreateGenericList(parameterType);
    Object ^ tool = Activator::CreateInstance(toolType);
    toolType->GetProperty("Id")->SetValue(tool, "90", nullptr);
    toolType->GetProperty("Name")->SetValue(tool, "Thermal Tool", nullptr);
    toolType->GetProperty("Parameters")->SetValue(tool, parameters, nullptr);

    Object ^ tools = CreateGenericList(toolType);
    AddToList(tools, tool);
    Object ^ device = Activator::CreateInstance(deviceType,
        gcnew array<Object ^>{
            "0", "Intel(R) Core(TM) i7-10750H CPU @ 2.60GHz", nullptr, tools, nullptr, nullptr});
    Object ^ devices = CreateGenericList(deviceType);
    AddToList(devices, device);

    Object ^ loadingStatus = Enum::Parse(loadingStatusType, "Success", false);
    Object ^ module = Activator::CreateInstance(
        moduleType, gcnew array<Object ^>{"13", "CPU", devices, loadingStatus, nullptr});
    Object ^ modules = CreateGenericList(moduleType);
    AddToList(modules, module);
    return modules;
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
    Stopwatch ^ totalWatch = StartTiming(sink);
    if (context->loaded) {
        TraceTiming(sink, "initialize_cached", totalWatch);
        return true;
    }

    context->addinDirectory = gcnew String(addinDirectory);
    Stopwatch ^ fileCheckWatch = StartTiming(sink);
    if (!File::Exists(CombinePath(context->addinDirectory, kClientDll)) ||
        !File::Exists(CombinePath(context->addinDirectory, kCoreDll)) ||
        !File::Exists(CombinePath(context->addinDirectory, kRpcClientDll)) ||
        !File::Exists(CombinePath(context->addinDirectory, kServerExe))) {
        TraceTiming(sink, "initialize_file_check", fileCheckWatch);
        TraceTiming(sink, "initialize_total", totalWatch);
        SetDiagnosticsText(sink, "Lenovo Hardware Scan LdeApi files were not found.");
        return false;
    }
    TraceTiming(sink, "initialize_file_check", fileCheckWatch);

    try {
        Stopwatch ^ resolverWatch = StartTiming(sink);
        LenovoAssemblyResolver::EnsureInstalled(context->addinDirectory);
        Environment::SetEnvironmentVariable("PATH",
            String::Concat(context->addinDirectory, ";", Environment::GetEnvironmentVariable("PATH")),
            EnvironmentVariableTarget::Process);
        TraceTiming(sink, "initialize_resolver_path", resolverWatch);

        Stopwatch ^ assemblyWatch = StartTiming(sink);
        context->coreAssembly = Assembly::LoadFrom(CombinePath(context->addinDirectory, kCoreDll));
        context->clientAssembly = Assembly::LoadFrom(CombinePath(context->addinDirectory, kClientDll));
        Assembly::LoadFrom(CombinePath(context->addinDirectory, kRpcClientDll));
        TraceTiming(sink, "initialize_load_assemblies", assemblyWatch);

        Stopwatch ^ reflectionWatch = StartTiming(sink);
        context->clientType = context->clientAssembly->GetType("Lenovo.LdeApi.Client.LdeApiClient", true);
        context->resultType = context->coreAssembly->GetType("Lenovo.LdeApi.Core.Models.Result", true);
        Type ^ serverInitModeType = context->coreAssembly->GetType("Lenovo.LdeApi.Core.Enums.ServerInitMode", true);
        Object ^ serverInitMode = Enum::Parse(serverInitModeType, "AsParentProcess", false);

        context->loadModulesMethod = context->clientType->GetMethod("LoadModules");
        context->getAvailableModulesMethod = context->clientType->GetMethod("GetAvailableModules");
        context->startExecutionMethod = context->clientType->GetMethod("StartExecution");
        context->disposeMethod = context->clientType->GetMethod("Dispose", Type::EmptyTypes);
        if (context->loadModulesMethod == nullptr || context->getAvailableModulesMethod == nullptr ||
            context->startExecutionMethod == nullptr || context->disposeMethod == nullptr) {
            TraceTiming(sink, "initialize_reflection", reflectionWatch);
            TraceTiming(sink, "initialize_total", totalWatch);
            SetDiagnosticsText(sink, "Lenovo Hardware Scan reflection members were not found.");
            return false;
        }
        TraceTiming(sink, "initialize_reflection", reflectionWatch);

        Stopwatch ^ logPathWatch = StartTiming(sink);
        String ^ logPath = Path::Combine(Path::GetTempPath(), "CaseDashLenovoHardwareScanLogs");
        Directory::CreateDirectory(logPath);
        if (!logPath->EndsWith(Path::DirectorySeparatorChar.ToString())) {
            logPath = String::Concat(logPath, Path::DirectorySeparatorChar);
        }
        TraceTiming(sink, "initialize_log_path", logPathWatch);

        Stopwatch ^ createClientWatch = StartTiming(sink);
        String ^ processMonitorName = System::Diagnostics::Process::GetCurrentProcess()->ProcessName;
        context->client = Activator::CreateInstance(
            context->clientType, gcnew array<Object ^>{logPath, nullptr, nullptr, serverInitMode, processMonitorName});
        TraceTiming(sink, "initialize_create_client", createClientWatch);

        pin_ptr<const wchar_t> pinnedAssembly = PtrToStringChars(CombinePath(context->addinDirectory, kClientDll));
        sink.TraceAssemblyLoaded(pinnedAssembly);
        context->loaded = true;
        SetDiagnosticsText(sink, "Lenovo Hardware Scan runtime initialized.");
        TraceTiming(sink, "initialize_total", totalWatch);
        return true;
    } catch (Exception ^ ex) {
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        sink.SetDiagnostics(pinnedDiagnostics);
        sink.TraceInitializeException(pinnedDiagnostics);
        TraceTiming(sink, "initialize_total", totalWatch);
        return false;
    }
}

bool EnsureModulesLoaded(LenovoRuntimeContext ^ context,
    const LenovoHardwareScanCaptureOptions& options,
    LenovoHardwareScanCaptureSink& sink) {
    array<String ^> ^ modules = BuildRequestedModules(options);
    String ^ signature = ModuleSignature(modules);
    if (modules->Length == 0) {
        SetDiagnosticsText(sink, "Lenovo Hardware Scan has no requested modules.");
        return false;
    }

    if (context->loadedModules != nullptr && EnumerableCount(context->loadedModules) > 0 &&
        String::Equals(context->loadedModuleSignature, signature, StringComparison::Ordinal)) {
        Stopwatch ^ cachedWatch = StartTiming(sink);
        TraceTiming(sink, "module_load_cached", cachedWatch);
        return true;
    }

    context->loadedModules = nullptr;
    context->loadedModuleSignature = nullptr;
    Stopwatch ^ invokeWatch = StartTiming(sink);
    Task ^ task =
        safe_cast<Task ^>(context->loadModulesMethod->Invoke(context->client, gcnew array<Object ^>{modules}));
    TraceTiming(sink, "module_load_invoke", invokeWatch);
    Stopwatch ^ waitWatch = StartTiming(sink);
    if (!WaitTask(task, kLoadModulesTimeoutMs, sink, "Lenovo Hardware Scan module load timed out.")) {
        TraceTiming(sink, "module_load_wait", waitWatch);
        return false;
    }
    TraceTiming(sink, "module_load_wait", waitWatch);

    Stopwatch ^ resultWatch = StartTiming(sink);
    context->loadedModules = TaskResult(task);
    context->loadedModuleSignature = signature;
    String ^ loadSummary = ModuleLoadSummary(modules, context->loadedModules);
    pin_ptr<const wchar_t> pinnedLoadSummary = PtrToStringChars(loadSummary);
    sink.TraceModuleLoadResult(pinnedLoadSummary);
    TraceTiming(sink, "module_load_result", resultWatch);
    if (EnumerableCount(context->loadedModules) == 0) {
        SetDiagnosticsText(sink, "Lenovo Hardware Scan loaded no thermal modules.");
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

    Stopwatch ^ totalWatch = StartTiming(sink);
    try {
        Stopwatch ^ modulesWatch = StartTiming(sink);
        if (!EnsureModulesLoaded(context, options, sink)) {
            TraceTiming(sink, "ensure_modules_loaded", modulesWatch);
            TraceTiming(sink, "capture_total", totalWatch);
            return false;
        }
        TraceTiming(sink, "ensure_modules_loaded", modulesWatch);

        LenovoExecutionCapture ^ capture = gcnew LenovoExecutionCapture(sink, context->resultType);
        Stopwatch ^ executionInvokeWatch = StartTiming(sink);
        Task ^ task = safe_cast<Task ^>(context->startExecutionMethod->Invoke(
            context->client, gcnew array<Object ^>{capture->Callback, context->loadedModules, true, false}));
        TraceTiming(sink, "start_execution_invoke", executionInvokeWatch);
        Stopwatch ^ executionWaitWatch = StartTiming(sink);
        if (!WaitTask(task, kExecutionTimeoutMs, sink, "Lenovo Hardware Scan thermal execution timed out.")) {
            TraceTiming(sink, "start_execution_wait", executionWaitWatch);
            TraceTiming(sink, "capture_total", totalWatch);
            return false;
        }
        TraceTiming(sink, "start_execution_wait", executionWaitWatch);

        Stopwatch ^ executionResultWatch = StartTiming(sink);
        Object ^ result = TaskResult(task);
        const bool executionStarted = result != nullptr && Convert::ToBoolean(result);
        TraceTiming(sink, "start_execution_result", executionResultWatch);
        if (!executionStarted && capture->TemperatureCount == 0) {
            SetDiagnosticsText(sink, "Lenovo Hardware Scan thermal execution returned no telemetry.");
            TraceTiming(sink, "capture_total", totalWatch);
            return false;
        }
        TraceTiming(sink, "capture_total", totalWatch);
        return capture->TemperatureCount > 0;
    } catch (Exception ^ ex) {
        context->loadedModules = nullptr;
        context->loadedModuleSignature = nullptr;
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        sink.SetDiagnostics(pinnedDiagnostics);
        sink.TraceSnapshotException(pinnedDiagnostics);
        TraceTiming(sink, "capture_total", totalWatch);
        return false;
    }
}

bool InvokeGetAvailableModules(LenovoRuntimeContext ^ context, LenovoHardwareScanCaptureSink& sink) {
    Stopwatch ^ invokeWatch = StartTiming(sink);
    Task ^ task = safe_cast<Task ^>(context->getAvailableModulesMethod->Invoke(context->client, gcnew array<Object ^>{}));
    TraceTiming(sink, "get_available_modules_invoke", invokeWatch);
    Stopwatch ^ waitWatch = StartTiming(sink);
    if (!WaitTask(task, kLoadModulesTimeoutMs, sink, "Lenovo Hardware Scan available-module query timed out.")) {
        TraceTiming(sink, "get_available_modules_wait", waitWatch);
        return false;
    }
    TraceTiming(sink, "get_available_modules_wait", waitWatch);

    Stopwatch ^ resultWatch = StartTiming(sink);
    Object ^ available = TaskResult(task);
    Object ^ modules = GetProperty(available, "Modules");
    String ^ summary = String::Format(
        Globalization::CultureInfo::InvariantCulture, "available_count={0}", EnumerableCount(modules));
    pin_ptr<const wchar_t> pinnedSummary = PtrToStringChars(summary);
    sink.TraceModuleLoadResult(pinnedSummary);
    TraceTiming(sink, "get_available_modules_result", resultWatch);
    return true;
}

bool StartCpuThermalToolExecution(LenovoRuntimeContext ^ context,
    Object ^ modules,
    LenovoHardwareScanCaptureSink& sink,
    Stopwatch ^ totalWatch) {
    LenovoExecutionCapture ^ capture = gcnew LenovoExecutionCapture(sink, context->resultType);
    Stopwatch ^ executionInvokeWatch = StartTiming(sink);
    Task ^ task = safe_cast<Task ^>(context->startExecutionMethod->Invoke(
        context->client, gcnew array<Object ^>{capture->Callback, modules, true, false}));
    TraceTiming(sink, "start_execution_invoke", executionInvokeWatch);
    Stopwatch ^ executionWaitWatch = StartTiming(sink);
    if (!WaitTask(task, kExecutionTimeoutMs, sink, "Lenovo Hardware Scan thermal execution timed out.")) {
        TraceTiming(sink, "start_execution_wait", executionWaitWatch);
        TraceTiming(sink, "probe_total", totalWatch);
        return false;
    }
    TraceTiming(sink, "start_execution_wait", executionWaitWatch);

    Stopwatch ^ executionResultWatch = StartTiming(sink);
    Object ^ result = TaskResult(task);
    const bool executionStarted = result != nullptr && Convert::ToBoolean(result);
    TraceTiming(sink, "start_execution_result", executionResultWatch);
    if (!executionStarted && capture->TemperatureCount == 0) {
        SetDiagnosticsText(sink, "Lenovo Hardware Scan thermal execution returned no telemetry.");
        TraceTiming(sink, "probe_total", totalWatch);
        return false;
    }
    TraceTiming(sink, "probe_total", totalWatch);
    return capture->TemperatureCount > 0;
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

bool ProbeLenovoHardwareScanCpuThermalTool(const wchar_t* addinDirectory,
    LenovoHardwareScanLdeProbeMode mode,
    LenovoHardwareScanCaptureSink& sink) {
    LenovoRuntimeContext ^ context = gcnew LenovoRuntimeContext();
    if (!InitializeLenovoRuntime(context, addinDirectory, sink)) {
        DisposeLenovoRuntime(context);
        return false;
    }

    Stopwatch ^ totalWatch = StartTiming(sink);
    try {
        Object ^ modules = nullptr;
        if (mode == LenovoHardwareScanLdeProbeMode::AvailableModulesThenManualCpuThermalToolExecution) {
            Stopwatch ^ availableWatch = StartTiming(sink);
            if (!InvokeGetAvailableModules(context, sink)) {
                TraceTiming(sink, "probe_get_available_modules", availableWatch);
                TraceTiming(sink, "probe_total", totalWatch);
                DisposeLenovoRuntime(context);
                return false;
            }
            TraceTiming(sink, "probe_get_available_modules", availableWatch);
        }

        if (mode == LenovoHardwareScanLdeProbeMode::LoadModulesThenCpuThermalToolExecution) {
            LenovoHardwareScanCaptureOptions options;
            options.includeCpuTemperature = true;
            options.includeGpuTemperature = false;
            options.includeStorageTemperature = false;
            options.includeMotherboardTemperature = false;
            options.includeBatteryTemperature = false;
            Stopwatch ^ modulesWatch = StartTiming(sink);
            if (!EnsureModulesLoaded(context, options, sink)) {
                TraceTiming(sink, "probe_ensure_modules_loaded", modulesWatch);
                TraceTiming(sink, "probe_total", totalWatch);
                DisposeLenovoRuntime(context);
                return false;
            }
            TraceTiming(sink, "probe_ensure_modules_loaded", modulesWatch);
            modules = context->loadedModules;
        } else {
            Stopwatch ^ payloadWatch = StartTiming(sink);
            modules = BuildManualCpuThermalToolModules(context->coreAssembly);
            TraceTiming(sink, "probe_manual_payload", payloadWatch);
        }

        const bool captured = StartCpuThermalToolExecution(context, modules, sink, totalWatch);
        DisposeLenovoRuntime(context);
        return captured;
    } catch (Exception ^ ex) {
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        sink.SetDiagnostics(pinnedDiagnostics);
        sink.TraceSnapshotException(pinnedDiagnostics);
        TraceTiming(sink, "probe_total", totalWatch);
        DisposeLenovoRuntime(context);
        return false;
    }
}
