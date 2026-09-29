#include "CrashHandler.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <exception>

namespace CrashHandler {

namespace {

    void (*_log)(const char* report) = nullptr;
    /// Static: the crash may be a stack overflow or a broken heap.
    char _report[4096];
    size_t _used = 0;

    void _append(const char* format, ...) {
        va_list args;
        va_start(args, format);
        const int written = vsnprintf(_report + _used, sizeof _report - _used, format, args);
        va_end(args);
        if (written > 0) {
            _used = std::min(_used + written, sizeof _report - 1);
        }
    }

    const char* _codeName(const DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "Access Violation";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array Bounds Exceeded";
            case EXCEPTION_BREAKPOINT: return "Breakpoint";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "Datatype Misalignment";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "Float Divide by Zero";
            case EXCEPTION_FLT_INVALID_OPERATION: return "Float Invalid Operation";
            case EXCEPTION_FLT_OVERFLOW: return "Float Overflow";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "Illegal Instruction";
            case EXCEPTION_IN_PAGE_ERROR: return "In Page Error";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer Divide by Zero";
            case EXCEPTION_INT_OVERFLOW: return "Integer Overflow";
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "Noncontinuable Exception";
            case EXCEPTION_PRIV_INSTRUCTION: return "Privileged Instruction";
            case EXCEPTION_STACK_OVERFLOW: return "Stack Overflow";
            case 0xE06D7363: return "C++ Exception";
            default: return "Unknown Exception";
        }
    }

    [[noreturn]] void _crash(const char* source, const char* detail, const EXCEPTION_POINTERS* info) {
        // A crash inside _log ends at once; another thread's waits for the first report
        static std::atomic<DWORD> owner = 0;
        if (DWORD first = 0; !owner.compare_exchange_strong(first, GetCurrentThreadId())) {
            if (first == GetCurrentThreadId()) {
                TerminateProcess(GetCurrentProcess(), 1);
            }
            Sleep(INFINITE);
        }

        SYSTEMTIME time;
        GetLocalTime(&time);
        wchar_t path[MAX_PATH];
        char exe[MAX_PATH * 3] = "?";
        if (const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH)) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length), exe, sizeof exe - 1,
                                                  nullptr, nullptr);
            exe[std::max(bytes, 0)] = '\0';
        }
        _append("=== Application Crash Report ===\nTimestamp: %04u-%02u-%02u %02u:%02u:%02u\nApplication: %s\n"
                "Source: %s\n%s%s", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, exe,
                source, detail, *detail ? "\n" : "");

        if (info && info->ExceptionRecord) {
            const EXCEPTION_RECORD& record = *info->ExceptionRecord;
            _append("\nException: 0x%08lX %s\nFlags: 0x%lX\nAddress: %p\n", record.ExceptionCode,
                    _codeName(record.ExceptionCode), record.ExceptionFlags, record.ExceptionAddress);
            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
                const ULONG_PTR access = record.ExceptionInformation[0];
                _append("Access: %s 0x%llX\n", access == 0 ? "Read" : access == 1 ? "Write" : "Execute",
                        static_cast<unsigned long long>(record.ExceptionInformation[1]));
            }
        }
        if (info && info->ContextRecord) {
            const CONTEXT& context = *info->ContextRecord;
            _append("\nRAX 0x%llX\nRBX 0x%llX\nRCX 0x%llX\nRDX 0x%llX\nRSP 0x%llX\nRBP 0x%llX\nRIP 0x%llX\n",
                    context.Rax, context.Rbx, context.Rcx, context.Rdx, context.Rsp, context.Rbp, context.Rip);
        }

        if (_log) {
            _log(_report);
        }
        TerminateProcess(GetCurrentProcess(), 1);
        __assume(false);
    }

    LONG WINAPI _unhandled(EXCEPTION_POINTERS* info) {
        _crash("Unhandled exception", "", info);
    }

    void _terminate() {
        // Rethrown to read the message: a thrown object is not known to be a std::exception
        try {
            if (const std::exception_ptr active = std::current_exception()) {
                std::rethrow_exception(active);
            }
            _crash("std::terminate", "No active exception", nullptr);
        } catch (const std::exception& e) {
            _crash("std::terminate", e.what(), nullptr);
        } catch (...) {
            _crash("std::terminate", "Not a std::exception", nullptr);
        }
    }

    void _invalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                           const unsigned line, uintptr_t) {
        // Only the debug CRT names them
        char detail[512];
        snprintf(detail, sizeof detail, "%ls in %ls at %ls:%u", expression ? expression : L"?",
                 function ? function : L"?", file ? file : L"?", line);
        _crash("Invalid parameter", detail, nullptr);
    }

} // anonymous namespace

void Install(void (*log)(const char* report)) {
    _log = log;
    SetUnhandledExceptionFilter(_unhandled);
    std::set_terminate(_terminate);
    _set_purecall_handler([] { _crash("Pure virtual call", "", nullptr); });
    _set_invalid_parameter_handler(_invalidParameter);
    // abort() raises it before the process goes; faults reach the unhandled filter
    signal(SIGABRT, [](int) { _crash("abort()", "", nullptr); });
}

}
