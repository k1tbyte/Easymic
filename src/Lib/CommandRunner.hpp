#ifndef EASYMIC_COMMANDRUNNER_HPP
#define EASYMIC_COMMANDRUNNER_HPP

#include <windows.h>

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
        /// CP_ACP, not UTF-8: the command was typed into an ANSI edit control like every other
        /// string in the app, so a Cyrillic path would come back mangled from a UTF-8 decode.
        inline std::wstring ToWide(const std::string& text) {
            const int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
            if (size <= 1) {
                return {};
            }

            std::wstring result(size - 1, L'\0');
            MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, result.data(), size);
            return result;
        }

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
            Detail::Split(Detail::ToWide(command), file, params);

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
}

#endif //EASYMIC_COMMANDRUNNER_HPP
