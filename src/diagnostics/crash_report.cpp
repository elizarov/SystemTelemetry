#include "diagnostics/crash_report.h"

#include <windows.h>

#include <cstdint>
#include <dbghelp.h>
#include <string>
#include <string_view>
#include <utility>

#include "build_version.h"
#include "diagnostics/constants.h"
#include "diagnostics/diagnostics.h"
#include "util/file_path.h"
#include "util/paths.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

DiagnosticsOptions g_diagnosticsOptions;
LONG g_handlingCrash = 0;
constexpr char kAppendBinaryMode[] = "ab";
constexpr char kWriteBinaryMode[] = "wb";

std::string CrashReportFileName() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return FormatText(
        "casedash_crash_%04u%02u%02u_%02u%02u%02u_%03u_%lu",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()));
}

FilePath PathWithSuffix(const FilePath& path, std::string_view suffix) {
    std::string text = FormatText("%s%.*s", path.string().c_str(), static_cast<int>(suffix.size()), suffix.data());
    return FilePath(std::move(text));
}

FilePath ResolveCrashOutputBase() {
    const std::string fileName = CrashReportFileName();
    FilePath base = GetWorkingDirectory() / FilePath(fileName);
    FILE* probe = nullptr;
    const FilePath probePath = PathWithSuffix(base, ".tmp");
    if (fopen_s(&probe, probePath.string().c_str(), kWriteBinaryMode) == 0 && probe != nullptr) {
        fclose(probe);
        RemoveFileIfExists(probePath);
        return base;
    }
    return TempDirectoryPath() / FilePath(fileName);
}

std::string ExceptionCodeText(DWORD code) {
    return FormatText("0x%08lX", static_cast<unsigned long>(code));
}

std::string PointerText(const void* address) {
    return FormatText(
        "0x%0*llX",
        static_cast<int>(sizeof(void*) * 2),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)));
}

std::string ModulePathForAddress(void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address),
            &module)) {
        return {};
    }

    std::string path(MAX_PATH, '\0');
    for (;;) {
        const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size() - 1) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

void AppendLine(std::string& text, const char* key, const std::string& value) {
    AppendFormat(text, "%s: %s\r\n", key, value.c_str());
}

std::string BuildCrashReportText(const FilePath& dumpPath, EXCEPTION_POINTERS* exceptionPointers) {
    const EXCEPTION_RECORD* record = exceptionPointers != nullptr ? exceptionPointers->ExceptionRecord : nullptr;
    const void* exceptionAddress = record != nullptr ? record->ExceptionAddress : nullptr;

    std::string text;
    AppendLine(text, "event", "unhandled_exception");
    AppendLine(text, "timestamp", Trace::FormatTimestamp());
    AppendLine(text, "version", casedash::version::kVersion);
    AppendLine(text, "build_kind", casedash::version::kBuildKind);
    AppendLine(text, "git_commit", casedash::version::kGitCommit);
    AppendLine(text, "git_dirty", casedash::version::kGitDirty ? "yes" : "no");
    AppendLine(text, "process_id", FormatText("%lu", GetCurrentProcessId()));
    AppendLine(text, "thread_id", FormatText("%lu", GetCurrentThreadId()));
    AppendLine(text, "exception_code", record != nullptr ? ExceptionCodeText(record->ExceptionCode) : "unknown");
    AppendLine(text, "exception_address", PointerText(exceptionAddress));
    AppendLine(text, "faulting_module", ModulePathForAddress(const_cast<void*>(exceptionAddress)));
    if (const auto executablePath = GetExecutablePath(); executablePath.has_value()) {
        AppendLine(text, "executable", executablePath->string());
    }
    AppendLine(text, "working_directory", GetWorkingDirectory().string());
    AppendLine(text, "command_line", GetCommandLineA());
    AppendLine(text, "minidump", dumpPath.string());
    return text;
}

bool WriteMinidump(const FilePath& dumpPath, EXCEPTION_POINTERS* exceptionPointers) {
    HANDLE dumpFile = CreateFileA(
        dumpPath.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;

    const BOOL written = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        dumpFile,
        MiniDumpWithDataSegs,
        exceptionPointers != nullptr ? &exceptionInfo : nullptr,
        nullptr,
        nullptr);
    CloseHandle(dumpFile);
    return written != FALSE;
}

void AppendCrashTrace(const FilePath& reportPath, const FilePath& dumpPath, EXCEPTION_POINTERS* exceptionPointers) {
    if (!g_diagnosticsOptions.trace) {
        return;
    }

    const FilePath tracePath =
        ResolveDiagnosticsOutputPath(GetWorkingDirectory(), g_diagnosticsOptions.tracePath, kDefaultTraceFileName);
    FILE* traceFile = nullptr;
    if (fopen_s(&traceFile, tracePath.string().c_str(), kAppendBinaryMode) != 0 || traceFile == nullptr) {
        return;
    }

    const EXCEPTION_RECORD* record = exceptionPointers != nullptr ? exceptionPointers->ExceptionRecord : nullptr;
    const std::string code = record != nullptr ? ExceptionCodeText(record->ExceptionCode) : "unknown";
    const std::string address = PointerText(record != nullptr ? record->ExceptionAddress : nullptr);
    const std::string reportText = reportPath.string();
    const std::string dumpText = dumpPath.string();
    Trace trace(traceFile);
    trace.WriteFmt(
        TracePrefix::Crash,
        RES_STR("unhandled_exception code=\"%s\" address=\"%s\" report=\"%s\" minidump=\"%s\""),
        code.c_str(),
        address.c_str(),
        reportText.c_str(),
        dumpText.c_str());
    fclose(traceFile);
}

LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exceptionPointers) {
    if (InterlockedExchange(&g_handlingCrash, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const FilePath outputBase = ResolveCrashOutputBase();
    const FilePath dumpPath = PathWithSuffix(outputBase, ".dmp");
    const FilePath reportPath = PathWithSuffix(outputBase, ".txt");
    const bool dumpWritten = WriteMinidump(dumpPath, exceptionPointers);
    std::string reportText = BuildCrashReportText(dumpPath, exceptionPointers);
    AppendLine(reportText, "minidump_written", dumpWritten ? "yes" : "no");
    WriteFileBinary(reportPath, reportText);
    AppendCrashTrace(reportPath, dumpPath, exceptionPointers);
    const std::string debugText = FormatText("CaseDash crash report written to %s\n", reportPath.string().c_str());
    OutputDebugStringA(debugText.c_str());
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashReportHandler(const DiagnosticsOptions& diagnosticsOptions) {
    g_diagnosticsOptions = diagnosticsOptions;
    SetUnhandledExceptionFilter(HandleUnhandledException);
}
