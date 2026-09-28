#include "CrashHandler.hpp"

#include <windows.h>
#include <eh.h>
#include <csignal>
#include <sstream>
#include <iomanip>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <array>

#if __cpp_lib_source_location >= 201907L
#include <source_location>
#endif

#include "Str.hpp"

namespace CrashHandler {
    namespace {
        struct HandlerState {
            LogCallback log;
            bool initialized = false;

            LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
            _purecall_handler previousPurecallHandler = nullptr;
            _invalid_parameter_handler previousInvalidParameterHandler = nullptr;
            std::terminate_handler previousTerminateHandler = nullptr;

        };

        HandlerState g_state;

        // A tray app gets no SIGINT or SIGTERM
        constexpr std::array<int, 4> HANDLED_SIGNALS = {SIGABRT, SIGFPE, SIGILL, SIGSEGV};

        std::string GetTimestamp() {
            SYSTEMTIME st;
            GetLocalTime(&st);

            std::ostringstream oss;
            oss << std::setfill('0')
                    << std::setw(4) << st.wYear << "-"
                    << std::setw(2) << st.wMonth << "-"
                    << std::setw(2) << st.wDay << " "
                    << std::setw(2) << st.wHour << ":"
                    << std::setw(2) << st.wMinute << ":"
                    << std::setw(2) << st.wSecond;

            return oss.str();
        }

        std::string GetExecutablePath() {
            std::array<wchar_t, MAX_PATH> path;
            const DWORD result = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            return result > 0 ? Str::WideToUtf8(std::wstring(path.data(), result)) : "Unknown";
        }


        constexpr std::string_view GetExceptionCodeDescription(DWORD code) noexcept {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION: return "Access Violation";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array Bounds Exceeded";
                case EXCEPTION_BREAKPOINT: return "Breakpoint";
                case EXCEPTION_DATATYPE_MISALIGNMENT: return "Datatype Misalignment";
                case EXCEPTION_FLT_DENORMAL_OPERAND: return "Float Denormal Operand";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "Float Divide by Zero";
                case EXCEPTION_FLT_INEXACT_RESULT: return "Float Inexact Result";
                case EXCEPTION_FLT_INVALID_OPERATION: return "Float Invalid Operation";
                case EXCEPTION_FLT_OVERFLOW: return "Float Overflow";
                case EXCEPTION_FLT_STACK_CHECK: return "Float Stack Check";
                case EXCEPTION_FLT_UNDERFLOW: return "Float Underflow";
                case EXCEPTION_ILLEGAL_INSTRUCTION: return "Illegal Instruction";
                case EXCEPTION_IN_PAGE_ERROR: return "In Page Error";
                case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer Divide by Zero";
                case EXCEPTION_INT_OVERFLOW: return "Integer Overflow";
                case EXCEPTION_INVALID_DISPOSITION: return "Invalid Disposition";
                case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "Noncontinuable Exception";
                case EXCEPTION_PRIV_INSTRUCTION: return "Privileged Instruction";
                case EXCEPTION_SINGLE_STEP: return "Single Step";
                case EXCEPTION_STACK_OVERFLOW: return "Stack Overflow";
                case 0xE06D7363: return "C++ Exception";
                default: return "Unknown Exception";
            }
        }

        constexpr std::string_view GetSignalDescription(int signal) noexcept {
            switch (signal) {
                case SIGABRT: return "SIGABRT (Abort)";
                case SIGFPE: return "SIGFPE (Floating Point Exception)";
                case SIGILL: return "SIGILL (Illegal Instruction)";
                case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
                default: return "Unknown Signal";
            }
        }

        void FormatCrashHeader(std::ostringstream &oss, const std::string &exceptionType, const std::string &source) {
            oss << "=== Application Crash Report ===\n"
                    << "Timestamp: " << GetTimestamp() << "\n"
                    << "Application: " << GetExecutablePath() << "\n"
                    << "Exception Type: " << exceptionType << "\n"
                    << "Source: " << source << "\n\n";
        }

        void FormatStructuredException(std::ostringstream &oss, EXCEPTION_POINTERS *pExceptionInfo) {
            if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) {
                return;
            }

            const EXCEPTION_RECORD *pRecord = pExceptionInfo->ExceptionRecord;
            const std::string_view description = GetExceptionCodeDescription(pRecord->ExceptionCode);

            oss << "Exception Code: 0x" << std::hex << std::uppercase << pRecord->ExceptionCode << "\n"
                    << "Exception Description: " << description << "\n"
                    << "Exception Flags: 0x" << std::hex << pRecord->ExceptionFlags << "\n"
                    << "Exception Address: 0x" << std::hex << reinterpret_cast<uintptr_t>(pRecord->ExceptionAddress) << "\n";

            // No message here on purpose: the thrown object cannot be assumed to be a
            // std::exception, and calling what() through a guessed vtable faults the crash
            // handler itself. The terminate handler rethrows instead, which is safe and exact.

            if (pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRecord->NumberParameters >= 2) {
                const char *accessType = [](ULONG_PTR type) {
                    switch (type) {
                        case 0: return "Read";
                        case 1: return "Write";
                        case 8: return "Execute";
                        default: return "Unknown";
                    }
                }(pRecord->ExceptionInformation[0]);

                oss << "Access Type: " << accessType << "\n"
                        << "Access Address: 0x" << std::hex << pRecord->ExceptionInformation[1] << "\n";
            }
        }

        void FormatRegisters(std::ostringstream &oss, EXCEPTION_POINTERS *pExceptionInfo) {
            if (!pExceptionInfo || !pExceptionInfo->ContextRecord) {
                return;
            }

            const CONTEXT *pContext = pExceptionInfo->ContextRecord;
            oss << "\nRegisters:\n";

#ifdef _WIN64
            oss << "RAX: 0x" << std::hex << pContext->Rax << "\n"
                    << "RBX: 0x" << std::hex << pContext->Rbx << "\n"
                    << "RCX: 0x" << std::hex << pContext->Rcx << "\n"
                    << "RDX: 0x" << std::hex << pContext->Rdx << "\n"
                    << "RSP: 0x" << std::hex << pContext->Rsp << "\n"
                    << "RBP: 0x" << std::hex << pContext->Rbp << "\n"
                    << "RIP: 0x" << std::hex << pContext->Rip << "\n";
#else
            oss << "EAX: 0x" << std::hex << pContext->Eax << "\n"
                    << "EBX: 0x" << std::hex << pContext->Ebx << "\n"
                    << "ECX: 0x" << std::hex << pContext->Ecx << "\n"
                    << "EDX: 0x" << std::hex << pContext->Edx << "\n"
                    << "ESP: 0x" << std::hex << pContext->Esp << "\n"
                    << "EBP: 0x" << std::hex << pContext->Ebp << "\n"
                    << "EIP: 0x" << std::hex << pContext->Eip << "\n";
#endif
        }

        std::string FormatExceptionInfo(EXCEPTION_POINTERS *pExceptionInfo, const std::string &source) {
            std::ostringstream oss;

            const std::string_view description = pExceptionInfo && pExceptionInfo->ExceptionRecord
                                                     ? GetExceptionCodeDescription(
                                                         pExceptionInfo->ExceptionRecord->ExceptionCode)
                                                     : "System Exception";

            FormatCrashHeader(oss, std::string(description), source);
            FormatStructuredException(oss, pExceptionInfo);
            FormatRegisters(oss, pExceptionInfo);

            return oss.str();
        }

        std::string FormatCppExceptionInfo(const std::string &exceptionType, const std::string &message,
                                           const std::string &source) {
            std::ostringstream oss;
            FormatCrashHeader(oss, "C++ Exception: " + exceptionType, source);
            oss << "Message: " << message << "\n";
            return oss.str();
        }

        void NotifyException(const std::string &info) {
            if (g_state.log) {
                try {
                    g_state.log(info);
                } catch (...) {
                    // Ignore callback exceptions
                }
            }
            TerminateProcess(GetCurrentProcess(), 1);
        }

        void HandleStructuredException(EXCEPTION_POINTERS *pExceptionInfo, const std::string &source) {
            const std::string info = FormatExceptionInfo(pExceptionInfo, source);
            NotifyException(info);
        }

        void HandleCppException(const std::string &exceptionType, const std::string &message,
                                const std::string &source) {
            const std::string info = FormatCppExceptionInfo(exceptionType, message, source);
            NotifyException(info);
        }

        std::pair<std::string, std::string> IdentifyCurrentException() noexcept {
            try {
                std::exception_ptr eptr = std::current_exception();
                if (eptr) {
                    std::rethrow_exception(eptr);
                }
                return {"Unknown", "No active exception"};
            } catch (const std::runtime_error &e) {
                return {"std::runtime_error", e.what()};
            } catch (const std::logic_error &e) {
                return {"std::logic_error", e.what()};
            } catch (const std::bad_alloc &e) {
                return {"std::bad_alloc", e.what()};
            } catch (const std::bad_cast &e) {
                return {"std::bad_cast", e.what()};
            } catch (const std::bad_typeid &e) {
                return {"std::bad_typeid", e.what()};
            } catch (const std::bad_exception &e) {
                return {"std::bad_exception", e.what()};
            } catch (const std::exception &e) {
                return {"std::exception", e.what()};
            } catch (...) {
                return {"Unknown C++ Exception", "Non-standard exception type or noexcept violation"};
            }
        }

        LONG WINAPI UnhandledExceptionFilter(_In_ EXCEPTION_POINTERS *pExceptionInfo) {
            HandleStructuredException(pExceptionInfo, "Unhandled Exception Filter");

            if (g_state.previousFilter) {
                return g_state.previousFilter(pExceptionInfo);
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }

        void PurecallHandler() {
            HandleStructuredException(nullptr, "Pure Virtual Function Call");

            if (g_state.previousPurecallHandler) {
                g_state.previousPurecallHandler();
            }
        }

        void InvalidParameterHandler(const wchar_t *expression, const wchar_t *function,
                                     const wchar_t *file, unsigned int line, uintptr_t) {
            // Travels as the source, so it reaches the report - a separate field would just be
            // overwritten by the header that HandleStructuredException builds next
            std::ostringstream oss;
            oss << "Invalid Parameter";
            if (expression) oss << " | expression: " << Str::WideToUtf8(expression);
            if (function) oss << " | function: " << Str::WideToUtf8(function);
            if (file) oss << " | file: " << Str::WideToUtf8(file) << ":" << line;

            HandleStructuredException(nullptr, oss.str());

            if (g_state.previousInvalidParameterHandler) {
                g_state.previousInvalidParameterHandler(expression, function, file, line, 0);
            }
        }

        void TerminateHandler() {
            const auto [exceptionType, message] = IdentifyCurrentException();
            HandleCppException(exceptionType, message, "std::terminate Handler");

            if (g_state.previousTerminateHandler) {
                g_state.previousTerminateHandler();
            }

            std::abort();
        }

        void SignalHandler(int signal) {
            const std::string_view signalName = GetSignalDescription(signal);
            HandleStructuredException(nullptr, std::string("Signal Handler: ") + std::string(signalName));
        }

        void SETranslatorFunction(unsigned int, EXCEPTION_POINTERS *pExceptionInfo) {
            HandleStructuredException(pExceptionInfo, "Structured Exception Translator");
            throw std::runtime_error("Structured exception translated to C++ exception");
        }

        void InstallSignalHandlers() {
            for (size_t i = 0; i < HANDLED_SIGNALS.size(); ++i) {
                signal(HANDLED_SIGNALS[i], SignalHandler);
            }
        }

    } // anonymous namespace

    bool Initialize(LogCallback log) {
        if (g_state.initialized) {
            return false;
        }

        g_state.log = std::move(log);

        g_state.previousFilter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
        g_state.previousPurecallHandler = _set_purecall_handler(PurecallHandler);
        g_state.previousInvalidParameterHandler = _set_invalid_parameter_handler(InvalidParameterHandler);
        g_state.previousTerminateHandler = std::set_terminate(TerminateHandler);

        InstallSignalHandlers();
#ifdef _MSC_VER
        _set_se_translator(SETranslatorFunction);
#endif

        g_state.initialized = true;
        return true;
    }

} // namespace CrashHandler
