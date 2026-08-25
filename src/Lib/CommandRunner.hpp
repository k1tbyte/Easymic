#ifndef EASYMIC_COMMANDRUNNER_HPP
#define EASYMIC_COMMANDRUNNER_HPP

#include <windows.h>

#include <functional>
#include <shellapi.h>
#include <string>
#include <thread>
#include <unordered_set>

#include "Process.hpp"
#include "Str.hpp"
#include "definitions.h"

/**
 * @brief Runs a user command line through the shell.
 *
 * Shell semantics on purpose: executables, shortcuts, folders, documents and URLs all work,
 * and a bare name is resolved through PATH. Shell builtins need an explicit "cmd /c ...".
 */
namespace CommandRunner {

    namespace Detail {
        inline bool IsExistingFile(const std::wstring& path) {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }

        /// Splits a command line into the target and its arguments.
        inline void Split(const std::wstring& command, std::wstring& file, std::wstring& params) {
            size_t end;

            if (command.starts_with(L'"')) {
                end = command.find(L'"', 1);
                file = command.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
                end = end == std::wstring::npos ? command.size() : end + 1;
            } else if (GetFileAttributesW(command.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // Whole line is a path - covers unquoted "C:\Program Files\..." and folders
                file = command;
                end = command.size();
            } else {
                // Longest prefix that is a real file, so unquoted paths with spaces still work.
                // Nothing matched means a PATH lookup, a URL or "cmd /c ..." - take the first token
                end = std::wstring::npos;
                for (size_t space = command.find(L' '); space != std::wstring::npos;
                     space = command.find(L' ', space + 1)) {
                    if (IsExistingFile(command.substr(0, space))) {
                        end = space;
                    }
                }

                end = end == std::wstring::npos ? command.find(L' ') : end;
                file = command.substr(0, end);
            }

            const size_t paramStart = end < command.size() ? command.find_first_not_of(L' ', end)
                                                           : std::wstring::npos;
            params = paramStart == std::wstring::npos ? std::wstring{} : command.substr(paramStart);
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

        struct WindowSearch {
            const std::wstring& imagePath;
            /// One image-path lookup per process instead of one per window on every pass
            std::unordered_set<DWORD> checked;
            HWND result = nullptr;
        };

        inline BOOL CALLBACK FindMainWindow(HWND window, LPARAM param) {
            auto* search = reinterpret_cast<WindowSearch*>(param);

            // Owned windows are dialogs and tooltips, never the one to raise
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) {
                return TRUE;
            }

            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (!search->checked.insert(processId).second) {
                return TRUE;
            }

            if (_wcsicmp(Process::GetNameByHWND(window).c_str(), search->imagePath.c_str()) != 0) {
                return TRUE;
            }

            search->result = window;
            return FALSE;
        }

        /**
         * @brief Raises the launched application.
         *
         * Windows grants foreground rights to the process started by the current foreground app.
         * A hotkey from a tray app is not that, so the new window opens behind everything, and a
         * single instance app (Sublime, VS Code) only flashes in the taskbar. Attaching to the
         * foreground input queue lifts the restriction for the moment we need it.
         */
        inline void BringToFront(const std::wstring& imagePath) {
            constexpr int AttemptCount = 20;
            constexpr int AttemptDelayMs = 100;

            // Kept across passes: an already inspected process is only interesting again once it
            // opens the window we are waiting for, and a fresh instance brings a fresh pid
            WindowSearch search{imagePath};

            for (int attempt = 0; attempt < AttemptCount && !search.result; attempt++) {
                // An already running app answers on the first pass - it should not pay the delay
                if (attempt) {
                    Sleep(AttemptDelayMs);
                }

                EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
            }

            const HWND window = search.result;
            if (!window || window == GetForegroundWindow()) {
                return;
            }

            const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const DWORD currentThread = GetCurrentThreadId();

            AttachThreadInput(currentThread, foregroundThread, TRUE);
            ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
            BringWindowToTop(window);
            SetForegroundWindow(window);
            AttachThreadInput(currentThread, foregroundThread, FALSE);
        }
    }

    /**
     * @brief Launches the command without blocking the caller.
     *
     * Always on a fresh thread: the hotkey worker queue is serial, and ShellExecuteEx can sit
     * for seconds on a UAC prompt or a slow shell handler - that would stall mic actions.
     */
    inline void Run(const std::string& command) {
        if (command.empty()) {
            return;
        }

        std::thread([command] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            std::wstring file, params;
            Detail::Split(Str::Utf8ToWide(command), file, params);

            SHELLEXECUTEINFOW info{sizeof(info)};
            // NOASYNC is required when the calling thread exits right after - this one does
            info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
            info.lpFile = file.c_str();
            info.lpParameters = params.empty() ? nullptr : params.c_str();
            info.nShow = SW_SHOWNORMAL;

            if (!ShellExecuteExW(&info)) {
                LOG_ERROR("Failed to run '%s': 0x%08lX", command.c_str(), GetLastError());
            } else if (Detail::IsExistingFile(file)) {
                Detail::BringToFront(file);
            }

            CoUninitialize();
        }).detach();
    }

    /**
     * @brief Runs the command and hands its output over once it has finished.
     *
     * The callback lands on this thread, not the caller's, and may be minutes late - it must not
     * touch anything that can be gone by then.
     */
    inline void RunCaptured(const std::string& command, std::function<void(std::string)> onFinished) {
        if (command.empty()) {
            return;
        }

        std::thread([command, onFinished = std::move(onFinished)] {
            onFinished(Detail::Capture(Str::Utf8ToWide(command)));
        }).detach();
    }
}

#endif //EASYMIC_COMMANDRUNNER_HPP
