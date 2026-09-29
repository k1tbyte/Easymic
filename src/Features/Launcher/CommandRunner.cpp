#include "CommandRunner.hpp"

#include <algorithm>
#include <thread>
#include <vector>

#include "CommandLine.hpp"
#include "ShellContext.hpp"
#include "ShellLaunch.hpp"
#include "Str.hpp"
#include "Windowing/WindowCatalog.hpp"
#include "definitions.h"

namespace CommandRunner {

namespace {

    constexpr DWORD TimeoutMs = 10000;
    constexpr size_t KeptBytes = 4096;
    constexpr size_t DisplayBytes = 120;
    constexpr DWORD PollMs = 20;

    struct Apartment {
        Apartment() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
        ~Apartment() { CoUninitialize(); }
        Apartment(const Apartment&) = delete;
        Apartment& operator=(const Apartment&) = delete;
    };

    bool _pathExists(const std::wstring& path) {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    /// Home rather than an empty folder: `wt -d ""` only fails, and home is where a terminal opens anyway.
    std::string _expandDir(std::string command, const HWND context) {
        if (!command.contains(CommandLine::DirToken)) {
            return command;
        }

        std::string folder = Str::WideToUtf8(ShellContext::ActiveFolder(context));
        if (folder.empty()) {
            wchar_t profile[MAX_PATH];
            if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
                folder = Str::WideToUtf8(profile);
            }
        }
        return CommandLine::ExpandDir(std::move(command), folder);
    }

    /// Console tools are split between UTF-8 and the OEM page, and nothing announces which.
    std::string _toUtf8(const std::string& consoleOutput) {
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, consoleOutput.c_str(), -1,
                                nullptr, 0) > 0) {
            return consoleOutput;
        }

        const int size = MultiByteToWideChar(CP_OEMCP, 0, consoleOutput.c_str(), -1, nullptr, 0);
        if (size <= 1) {
            return {};
        }

        std::wstring wide(size - 1, L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, consoleOutput.c_str(), -1, wide.data(), size);
        return Str::WideToUtf8(wide);
    }

    /// Through cmd (/d skips AutoRun, /s strips exactly the wrapping quotes): CreateProcess cannot run a .bat or resolve a PATH name.
    std::string _capture(const std::wstring& command) {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE readEnd = nullptr;
        HANDLE writeEnd = nullptr;
        if (!CreatePipe(&readEnd, &writeEnd, &attributes, 0)) {
            return {};
        }
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

        // USESTDHANDLES means all three: a command that reads stdin has to meet an end of file
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &attributes, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nul;
        startup.hStdOutput = writeEnd;
        startup.hStdError = writeEnd;

        std::wstring line = L"cmd.exe /d /s /c \"" + command + L"\"";
        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        CloseHandle(writeEnd);
        if (nul != INVALID_HANDLE_VALUE) {
            CloseHandle(nul);
        }

        if (!started) {
            CloseHandle(readEnd);
            LOG_ERROR("Failed to capture output: 0x%08lX", GetLastError());
            return {};
        }

        // Polled, not a blocking read: a child that leaks the pipe to a grandchild would hold this thread forever
        std::string output;
        char buffer[512];
        for (const ULONGLONG deadline = GetTickCount64() + TimeoutMs; GetTickCount64() < deadline;) {
            const bool exited = WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
            DWORD available = 0;
            if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) || (!available && exited)) {
                break;
            }
            if (!available) {
                WaitForSingleObject(process.hProcess, PollMs);
                continue;
            }

            DWORD read = 0;
            if (!ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) || !read) {
                break;
            }
            output.append(buffer, std::min<size_t>(read, KeptBytes - output.size()));
        }

        if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 1);
        }
        CloseHandle(readEnd);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        if (output.size() == KeptBytes) {
            output.resize(CommandLine::WholeCodePoints(output).size());
        }
        return CommandLine::Flatten(_toUtf8(output), DisplayBytes);
    }

} // anonymous namespace

void Run(const std::string& command, const HWND context) {
    if (command.empty()) {
        return;
    }

    std::thread([command, context] {
        const Apartment apartment;
        const std::string line = _expandDir(command, context);
        const CommandLine::Parts parts = CommandLine::Split(Str::Utf8ToWide(line), _pathExists);
        const std::vector<HWND> before = WindowCatalog::AppWindows();
        const HWND wasInFront = GetForegroundWindow();

        if (!ShellLaunch::Run(parts.File, parts.Params)) {
            LOG_ERROR("Shell out of reach, not started: '%s'", line.c_str());
            return;
        }
        ShellLaunch::RaiseNew(before, wasInFront);
    }).detach();
}

void RunCaptured(const std::string& command, const HWND context, std::function<void(std::string)> onFinished) {
    if (command.empty()) {
        return;
    }

    std::thread([command, context, onFinished = std::move(onFinished)] {
        const Apartment apartment;
        onFinished(_capture(Str::Utf8ToWide(_expandDir(command, context))));
    }).detach();
}

}
