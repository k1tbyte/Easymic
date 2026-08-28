#pragma once

#include <windows.h>

#include <functional>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>

#include "ShellContext.hpp"
#include "ShellLaunch.hpp"
#include "Str.hpp"
#include "Tokens.hpp"
#include "definitions.h"

/**
 * @brief Runs a user command line through the shell.
 *
 * Shell semantics on purpose: executables, shortcuts, folders, documents and URLs all work,
 * and a bare name is resolved through PATH. Shell builtins need an explicit "cmd /c ...".
 *
 * A command line may carry {dir}. It is resolved against the window handed in, so the caller
 * reads the foreground at the moment the action fires - by the time the thread below starts,
 * whatever we launch may already have taken it.
 */
namespace CommandRunner {

    namespace Detail {
        /**
         * @brief Splits a command line into the target and its arguments.
         *
         * A path with spaces has to be quoted, the way the shell has always asked for. Probing
         * every prefix for an existing file instead costs one filesystem call per space on every
         * launch, and on a network path each of those can block for seconds.
         */
        inline void Split(const std::wstring& command, std::wstring& file, std::wstring& params) {
            size_t end;

            if (command.starts_with(L'"')) {
                end = command.find(L'"', 1);
                file = command.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
                end = end == std::wstring::npos ? command.size() : end + 1;
            } else if (GetFileAttributesW(command.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // Whole line is a path - covers an unquoted "C:\Program Files\..." and folders
                file = command;
                end = command.size();
            } else {
                // A PATH lookup, a URL or "cmd /c ..." - the target is the first token
                end = command.find(L' ');
                file = command.substr(0, end);
            }

            const size_t paramStart = end < command.size() ? command.find_first_not_of(L' ', end)
                                                           : std::wstring::npos;
            params = paramStart == std::wstring::npos ? std::wstring{} : command.substr(paramStart);
        }

        /**
         * @brief Puts a resolved value into the command line as a single argument.
         *
         * A path with a space would otherwise arrive as two, and a template that quoted the
         * token itself must not get a second pair. Either way a trailing backslash is doubled
         * first: sitting right before the closing quote it escapes it instead, which is how
         * "C:\" reaches an argv parser as one unterminated argument.
         */
        inline std::string Quote(std::string value, const bool alreadyQuoted) {
            if (!alreadyQuoted && value.find(' ') == std::string::npos) {
                return value;
            }

            if (value.ends_with('\\')) {
                value += '\\';
            }

            return alreadyQuoted ? value : '"' + value + '"';
        }

        /**
         * @brief Substitutes the folder token against the window that was in front.
         *
         * Falls back to the profile directory rather than nothing: an empty substitution leaves
         * a command like `wt -d ""` that only fails, and home is where a terminal opens anyway.
         * A token the user already wrapped in quotes is substituted bare so they do not double.
         */
        inline std::string ExpandDir(std::string command, const HWND context) {
            if (!command.contains(Tokens::Dir)) {
                return command;
            }

            std::string folder = Str::WideToUtf8(ShellContext::ActiveFolder(context));
            if (folder.empty()) {
                wchar_t profile[MAX_PATH];
                if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
                    folder = Str::WideToUtf8(profile);
                }
            }

            const size_t span = std::string_view(Tokens::Dir).size();
            for (size_t at = command.find(Tokens::Dir); at != std::string::npos;) {
                const std::string value = Quote(folder, at > 0 && command[at - 1] == '"');
                command.replace(at, span, value);
                at = command.find(Tokens::Dir, at + value.size());
            }

            return command;
        }

        /// Console tools are split between UTF-8 and the OEM page, and nothing announces which.
        /// Valid UTF-8 is taken at its word, anything else can only be OEM.
        inline std::string ToUtf8(const std::string& consoleOutput) {
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

        /// A pill is one line - newlines would be drawn as boxes, so they become spaces
        inline std::string Flatten(std::string text, const size_t limit) {
            for (char& character : text) {
                if (character == '\r' || character == '\n' || character == '\t') {
                    character = ' ';
                }
            }

            const size_t first = text.find_first_not_of(' ');
            const size_t last = text.find_last_not_of(' ');
            text = first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);

            return text.size() > limit ? text.substr(0, limit) + "..." : text;
        }

        /**
         * @brief Runs the command line through cmd and pipes its output back.
         *
         * Through the interpreter, not ShellExecute: a pipe needs CreateProcess, which cannot run a
         * .bat, resolve a PATH name or understand a redirection on its own. cmd gives all of that
         * back, which is also what someone writing a command to read output from expects.
         * What it does not give back is the shell namespace - a URL or a document opens nothing.
         *
         * /d skips whatever AutoRun is configured to print into every shell, /s makes cmd strip
         * exactly the wrapping quotes and take the rest verbatim.
         */
        inline std::string Capture(const std::wstring& command) {
            constexpr DWORD TimeoutMs = 10000;
            constexpr size_t OutputLimit = 4096;
            constexpr DWORD PollMs = 20;

            SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
            HANDLE readEnd = nullptr;
            HANDLE writeEnd = nullptr;
            if (!CreatePipe(&readEnd, &writeEnd, &attributes, 0)) {
                return {};
            }

            SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

            // STARTF_USESTDHANDLES means all three, and a command that reads stdin has to meet an
            // end of file rather than an invalid handle
            HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &attributes, OPEN_EXISTING, 0, nullptr);

            STARTUPINFOW startup{sizeof(startup)};
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = nul;
            startup.hStdOutput = writeEnd;
            startup.hStdError = writeEnd;

            std::wstring line = L"cmd.exe /d /s /c \"" + command + L"\"";

            PROCESS_INFORMATION process{};
            // CreateProcess may write into the command line, so it cannot be a read-only buffer
            const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
            // Our own copies have to go, or the read end never sees EOF
            CloseHandle(writeEnd);
            if (nul != INVALID_HANDLE_VALUE) {
                CloseHandle(nul);
            }

            if (!started) {
                CloseHandle(readEnd);
                LOG_ERROR("Failed to capture output: 0x%08lX", GetLastError());
                return {};
            }

            std::string output;
            char buffer[512];
            const ULONGLONG deadline = GetTickCount64() + TimeoutMs;

            // Polled rather than a blocking read: a child that leaks the pipe to a grandchild would
            // otherwise hold this thread for as long as the app lives
            while (GetTickCount64() < deadline && output.size() < OutputLimit) {
                DWORD available = 0;
                if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) {
                    break;
                }

                if (!available) {
                    if (WaitForSingleObject(process.hProcess, PollMs) == WAIT_OBJECT_0) {
                        // Exited, but whatever it wrote last is still sitting in the pipe
                        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) || !available) {
                            break;
                        }
                    } else {
                        continue;
                    }
                }

                DWORD read = 0;
                if (!ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) || !read) {
                    break;
                }
                output.append(buffer, read);
            }

            if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 1);
            }

            CloseHandle(readEnd);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);

            return Flatten(ToUtf8(output), 120);
        }
    }

    /**
     * @brief Launches the command without blocking the caller.
     *
     * Always on a fresh thread: the hotkey worker queue is serial, and ShellExecuteEx can sit
     * for seconds on a UAC prompt or a slow shell handler - that would stall mic actions.
     */
    inline void Run(const std::string& command, const HWND context) {
        if (command.empty()) {
            return;
        }

        std::thread([command, context] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            const std::string line = Detail::ExpandDir(command, context);

            std::wstring file, params;
            Detail::Split(Str::Utf8ToWide(line), file, params);

            // Windows only lets the process the foreground app started take the foreground, and a
            // hotkey from a tray app is not that - so an app that is already running would only
            // flash in the taskbar. The snapshot is what says afterwards which window the launch
            // produced, since the shell hands nothing back.
            const std::vector<HWND> before = ShellLaunch::Switchable();
            const HWND wasInFront = GetForegroundWindow();

            // The shell runs it as the user rather than with our token, which is what keeps a
            // bound app off administrator rights
            if (!ShellLaunch::Run(file, params)) {
                SHELLEXECUTEINFOW info{sizeof(info)};
                // NOASYNC is required when the calling thread exits right after - this one does
                info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
                info.lpFile = file.c_str();
                info.lpParameters = params.empty() ? nullptr : params.c_str();
                info.nShow = SW_SHOWNORMAL;

                if (!ShellExecuteExW(&info)) {
                    LOG_ERROR("Failed to run '%s': 0x%08lX", line.c_str(), GetLastError());
                    CoUninitialize();
                    return;
                }
            }

            ShellLaunch::RaiseNew(before, wasInFront);
            CoUninitialize();
        }).detach();
    }

    /**
     * @brief Runs the command and hands its output over once it has finished.
     *
     * The callback lands on this thread, not the caller's, and may be minutes late - it must not
     * touch anything that can be gone by then.
     */
    inline void RunCaptured(const std::string& command, const HWND context,
                            std::function<void(std::string)> onFinished) {
        if (command.empty()) {
            return;
        }

        std::thread([command, context, onFinished = std::move(onFinished)] {
            // Only the substitution needs an apartment, and the command it feeds can run for
            // ten seconds - no reason to hold one for that long
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const std::wstring line = Str::Utf8ToWide(Detail::ExpandDir(command, context));
            CoUninitialize();

            onFinished(Detail::Capture(line));
        }).detach();
    }
}

