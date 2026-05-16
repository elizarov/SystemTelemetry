#include "telemetry/board/msi/board_msi_center_bridge.h"

#include <msclr\gcroot.h>
#include <vcclr.h>

#include "util/utf8.h"

#using < mscorlib.dll>
#using < System.dll>

using namespace System;
using namespace System::IO;
using namespace System::Reflection;

namespace {

constexpr char kCommonApiDll[] = "CS_CommonAPI.dll";
constexpr int kMsiCenterServicePort = 9999;

array<unsigned char> ^
    MsiCenterCurrentDataCommand() { return gcnew array<unsigned char>{0x05, 0x03, 0x01, 0x08, 0x01, 0x00, 0x00, 0x01}; }

    String
    ^
    ManagedStringFromUtf8(std::string_view text) {
        const std::wstring wide = WideFromUtf8(text);
        return gcnew String(wide.c_str());
    }

    void SetDiagnosticsUtf8(MsiCenterCaptureSink& sink, std::string_view text) {
    const std::wstring wide = WideFromUtf8(text);
    sink.SetDiagnostics(wide.c_str());
}

String ^
    CombinePath(
        String ^ directory, const char* fileName) { return Path::Combine(directory, ManagedStringFromUtf8(fileName)); }

    String
    ^
    MsiFanNameFromId(int id) {
        switch (id) {
            case 1:
                return "CPU Fan 1";
            case 2:
                return "PUMP Fan 1";
            case 3:
                return "PUMP Fan 2";
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
                return String::Format(Globalization::CultureInfo::InvariantCulture, "SYS Fan {0}", id - 3);
            case 12:
                return "Water Flow";
            case 13:
                return "Chipset Fan";
            case 14:
                return "MOS Fan";
            case 15:
                return "JAF Fan 1";
            case 16:
                return "JAF Fan 2";
        }
        return String::Format(Globalization::CultureInfo::InvariantCulture, "Fan {0}", id);
    }

    String
    ^
    MsiTemperatureNameFromId(int id) {
        switch (id) {
            case 1:
                return "CPU";
            case 2:
                return "CPU Socket";
            case 3:
                return "System";
            case 4:
                return "MOS";
            case 5:
                return "PCH";
            case 6:
                return "PCIe 1";
            case 7:
                return "PCIe 2";
            case 8:
                return "PCIe 3";
            case 9:
                return "PCIe 4";
            case 10:
                return "PCIe 5";
            case 11:
                return "M.2 1";
            case 12:
                return "M.2 2";
            case 13:
                return "M.2 3";
        }
        return String::Format(Globalization::CultureInfo::InvariantCulture, "Temperature {0}", id);
    }

    String
    ^
    ReadMsiFanName(Object ^ fanName, FieldInfo ^ fanNameBytesField, FieldInfo ^ fanNameLengthField, int fallbackId) {
        if (fanName != nullptr && fanNameBytesField != nullptr && fanNameLengthField != nullptr) {
            array<unsigned char> ^ bytes = dynamic_cast<array<unsigned char> ^>(fanNameBytesField->GetValue(fanName));
            Object ^ lengthObject = fanNameLengthField->GetValue(fanName);
            if (bytes != nullptr && lengthObject != nullptr) {
                const int length = Math::Min(
                    Convert::ToInt32(lengthObject, Globalization::CultureInfo::InvariantCulture), bytes->Length);
                if (length > 0) {
                    String ^ name = Text::Encoding::ASCII->GetString(bytes, 0, length)->Trim();
                    if (!String::IsNullOrWhiteSpace(name)) {
                        return name->TrimEnd(gcnew array<wchar_t>{wchar_t{}, static_cast<wchar_t>(' ')});
                    }
                }
            }
        }
        return MsiFanNameFromId(fallbackId);
    }

    MethodInfo
    ^
    FindJsonDeserializer(Type ^ jsonType) {
        for each (MethodInfo ^ method in jsonType->GetMethods(BindingFlags::Public | BindingFlags::Static)) {
            if (!String::Equals(method->Name, "JSONDeSerializer", StringComparison::Ordinal) ||
                !method->IsGenericMethodDefinition) {
                continue;
            }
            array<ParameterInfo ^> ^ parameters = method->GetParameters();
            if (parameters->Length == 1 && parameters[0]->ParameterType == String::typeid) {
                return method;
            }
        }
        return nullptr;
    }

    ref class MsiCenterRuntimeContext sealed {
public:
    String ^ msiCenterDirectory = nullptr;
    String ^ commonAssemblyPath = nullptr;
    Assembly ^ commonAssembly = nullptr;
    Type ^ clientType = nullptr;
    Type ^ jsonType = nullptr;
    Type ^ ccEngineType = nullptr;
    Type ^ fanNameType = nullptr;
    MethodInfo ^ sendDataMethod = nullptr;
    MethodInfo ^ jsonDeserializerMethod = nullptr;
    FieldInfo ^ fanCountsField = nullptr;
    FieldInfo ^ curFanIdField = nullptr;
    FieldInfo ^ curFanSpeedField = nullptr;
    FieldInfo ^ fanNameField = nullptr;
    FieldInfo ^ fanNameBytesField = nullptr;
    FieldInfo ^ fanNameLengthField = nullptr;
    FieldInfo ^ temperatureCountsField = nullptr;
    FieldInfo ^ curTempIdField = nullptr;
    FieldInfo ^ temperatureValueField = nullptr;
    bool loaded = false;
};

bool InitializeMsiCenterRuntime(
    MsiCenterRuntimeContext ^ context, const wchar_t* msiCenterDirectory, MsiCenterCaptureSink& sink) {
    if (context->loaded) {
        return true;
    }

    context->msiCenterDirectory = gcnew String(msiCenterDirectory);
    context->commonAssemblyPath = CombinePath(context->msiCenterDirectory, kCommonApiDll);
    if (!File::Exists(context->commonAssemblyPath)) {
        SetDiagnosticsUtf8(sink, "MSI Center CS_CommonAPI.dll was not found.");
        return false;
    }

    try {
        context->commonAssembly = Assembly::LoadFrom(context->commonAssemblyPath);
        context->clientType = context->commonAssembly->GetType("CS_CommonAPI.C_Client", true);
        context->jsonType = context->commonAssembly->GetType("CS_CommonAPI.EX_JSON", true);
        context->ccEngineType = context->commonAssembly->GetType("CS_CommonAPI.Struct_CC_Engine", true);
        context->fanNameType = context->commonAssembly->GetType("CS_CommonAPI.Struct_Fan_Name", true);
        context->sendDataMethod = context->clientType->GetMethod("SendData",
            BindingFlags::Public | BindingFlags::Static,
            nullptr,
            gcnew array<Type ^>{Int32::typeid, array<unsigned char>::typeid},
            nullptr);
        MethodInfo ^ genericDeserializer = FindJsonDeserializer(context->jsonType);
        context->jsonDeserializerMethod =
            genericDeserializer != nullptr ? genericDeserializer->MakeGenericMethod(context->ccEngineType) : nullptr;
        context->fanCountsField = context->ccEngineType->GetField("FanCounts");
        context->curFanIdField = context->ccEngineType->GetField("CurFanID");
        context->curFanSpeedField = context->ccEngineType->GetField("CurFanSpeed");
        context->fanNameField = context->ccEngineType->GetField("FanName");
        context->fanNameBytesField = context->fanNameType->GetField("byFan_Name");
        context->fanNameLengthField = context->fanNameType->GetField("FanNameLenth");
        context->temperatureCountsField = context->ccEngineType->GetField("TemperatureCounts");
        context->curTempIdField = context->ccEngineType->GetField("CurTempID");
        context->temperatureValueField = context->ccEngineType->GetField("TemperatureValue");

        if (context->sendDataMethod == nullptr || context->jsonDeserializerMethod == nullptr ||
            context->fanCountsField == nullptr || context->curFanIdField == nullptr ||
            context->curFanSpeedField == nullptr || context->fanNameField == nullptr ||
            context->fanNameBytesField == nullptr || context->fanNameLengthField == nullptr ||
            context->temperatureCountsField == nullptr || context->curTempIdField == nullptr ||
            context->temperatureValueField == nullptr) {
            SetDiagnosticsUtf8(sink, "MSI Center hardware-monitor reflection members were not found.");
            return false;
        }

        pin_ptr<const wchar_t> pinnedAssemblyPath = PtrToStringChars(context->commonAssemblyPath);
        sink.TraceAssemblyLoaded(pinnedAssemblyPath);
        context->loaded = true;
        SetDiagnosticsUtf8(sink, "MSI Center hardware-monitor runtime initialized.");
        return true;
    } catch (Exception ^ ex) {
        String ^ exceptionText = ex->ToString();
        pin_ptr<const wchar_t> pinnedDiagnostics = PtrToStringChars(exceptionText);
        const wchar_t* diagnostics = pinnedDiagnostics;
        sink.SetDiagnostics(diagnostics);
        sink.TraceInitializeException(diagnostics);
        return false;
    }
}

template <typename T> T GetFieldValueOr(FieldInfo ^ field, Object ^ owner, T fallback) {
    if (field == nullptr || owner == nullptr) {
        return fallback;
    }
    Object ^ value = field->GetValue(owner);
    return value != nullptr ? safe_cast<T>(value) : fallback;
}

bool CaptureMsiCenterSnapshot(
    MsiCenterRuntimeContext ^ context, const wchar_t* msiCenterDirectory, MsiCenterCaptureSink& sink) {
    if (!InitializeMsiCenterRuntime(context, msiCenterDirectory, sink)) {
        return false;
    }

    try {
        array<unsigned char> ^ bytes = safe_cast<array<unsigned char> ^>(context->sendDataMethod->Invoke(
            nullptr, gcnew array<Object ^>{kMsiCenterServicePort, MsiCenterCurrentDataCommand()}));
        if (bytes == nullptr || bytes->Length <= 1) {
            SetDiagnosticsUtf8(sink, "MSI Center hardware-monitor query returned no data.");
            return false;
        }

        String ^ json = Text::Encoding::UTF8->GetString(bytes);
        Object ^ ccEngine = context->jsonDeserializerMethod->Invoke(nullptr, gcnew array<Object ^>{json});
        if (ccEngine == nullptr) {
            SetDiagnosticsUtf8(sink, "MSI Center hardware-monitor JSON did not deserialize.");
            return false;
        }

        const int fanCount = Math::Max(0, GetFieldValueOr<int>(context->fanCountsField, ccEngine, 0));
        Array ^ fanIds = dynamic_cast<Array ^>(context->curFanIdField->GetValue(ccEngine));
        Array ^ fanSpeeds = dynamic_cast<Array ^>(context->curFanSpeedField->GetValue(ccEngine));
        Array ^ fanNames = dynamic_cast<Array ^>(context->fanNameField->GetValue(ccEngine));
        const int availableFans = Math::Min(fanCount,
            Math::Min(fanIds != nullptr ? fanIds->Length : 0,
                Math::Min(fanSpeeds != nullptr ? fanSpeeds->Length : 0, fanNames != nullptr ? fanNames->Length : 0)));
        for (int i = 0; i < availableFans; ++i) {
            const int id = Convert::ToInt32(fanIds->GetValue(i), Globalization::CultureInfo::InvariantCulture);
            const double rpm = Convert::ToDouble(fanSpeeds->GetValue(i), Globalization::CultureInfo::InvariantCulture);
            String ^ title =
                ReadMsiFanName(fanNames->GetValue(i), context->fanNameBytesField, context->fanNameLengthField, id);
            pin_ptr<const wchar_t> pinnedTitle = PtrToStringChars(title);
            sink.AddFanReading(pinnedTitle, rpm);
        }

        const int temperatureCount = Math::Max(0, GetFieldValueOr<int>(context->temperatureCountsField, ccEngine, 0));
        Array ^ temperatureIds = dynamic_cast<Array ^>(context->curTempIdField->GetValue(ccEngine));
        Array ^ temperatureValues = dynamic_cast<Array ^>(context->temperatureValueField->GetValue(ccEngine));
        const int availableTemperatures = Math::Min(temperatureCount,
            Math::Min(temperatureIds != nullptr ? temperatureIds->Length : 0,
                temperatureValues != nullptr ? temperatureValues->Length : 0));
        for (int i = 0; i < availableTemperatures; ++i) {
            const int id = Convert::ToInt32(temperatureIds->GetValue(i), Globalization::CultureInfo::InvariantCulture);
            const double celsius =
                Convert::ToDouble(temperatureValues->GetValue(i), Globalization::CultureInfo::InvariantCulture);
            String ^ title = MsiTemperatureNameFromId(id);
            pin_ptr<const wchar_t> pinnedTitle = PtrToStringChars(title);
            sink.AddTemperatureReading(pinnedTitle, celsius);
        }

        sink.TraceQuerySuccess(availableFans, availableTemperatures);
        SetDiagnosticsUtf8(sink, "MSI Center hardware-monitor query completed.");
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

struct MsiCenterRuntime::Impl {
    Impl() : context(gcnew MsiCenterRuntimeContext()) {}

    msclr::gcroot<MsiCenterRuntimeContext ^> context;
};

MsiCenterRuntime::MsiCenterRuntime() : impl_(std::make_unique<Impl>()) {}

MsiCenterRuntime::~MsiCenterRuntime() = default;

bool MsiCenterRuntime::Capture(const wchar_t* msiCenterDirectory, MsiCenterCaptureSink& sink) {
    return CaptureMsiCenterSnapshot(impl_->context, msiCenterDirectory, sink);
}
