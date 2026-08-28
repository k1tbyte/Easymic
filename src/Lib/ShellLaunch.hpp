#pragma once

#include <windows.h>

#include <comutil.h>
#include <exdisp.h>
#include <servprov.h>
#include <shldisp.h>
#include <shlobj.h>
#include <algorithm>
#include <string>
#include <vector>

#include "definitions.h"

/**
 * @brief Launching through explorer.exe instead of out of this process, and putting what comes
 *        up in front.
 *
 * A child inherits the token of whoever started it, and Easymic runs elevated whenever the user
 * asked it to skip the UAC prompt - which would hand administrator rights to every bound app,
 * silently and for no reason. The desktop shell is the one process that can start it as the user
 * instead.
 *
 * Nothing about the launch grants it the foreground, though. That belongs to whoever the user
 * last dealt with, so the window has to be raised from here once it exists.
 */
namespace ShellLaunch {

    namespace Detail {
        /// The desktop's shell view - the part of explorer.exe that can run something for us.
        inline ComPtr<IShellDispatch2> DesktopShell() {
            ComPtr<IShellWindows> windows;
            if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                        IID_PPV_ARGS(&windows)))) {
                return nullptr;
            }

            VARIANT desktopRoot{};
            long handle = 0;
            ComPtr<IDispatch> desktop;
            if (FAILED(windows->FindWindowSW(&desktopRoot, &desktopRoot, SWC_DESKTOP, &handle,
                                             SWFO_NEEDDISPATCH, &desktop)) || !desktop) {
                return nullptr;
            }

            ComPtr<IServiceProvider> provider;
            ComPtr<IShellBrowser> browser;
            ComPtr<IShellView> view;
            ComPtr<IDispatch> background;
            ComPtr<IShellFolderViewDual> folderView;
            ComPtr<IDispatch> application;
            ComPtr<IShellDispatch2> shell;

            if (FAILED(desktop.As(&provider))
                || FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))
                || FAILED(browser->QueryActiveShellView(&view))
                || FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))
                || FAILED(background.As(&folderView))
                || FAILED(folderView->get_Application(&application))
                || FAILED(application.As(&shell))) {
                return nullptr;
            }

            return shell;
        }

        /**
         * @brief Raises a window this process did not create.
         *
         * The foreground belongs to whoever the user last dealt with, and a tray app woken by a
         * hotkey is never that, so the request is refused on its own. Sharing the input queue of
         * the window in front is what makes it count as coming from the foreground - and both
         * calls are needed: with the queues shared it is BringWindowToTop that does the
         * activating, SetForegroundWindow alone still comes back refused.
         */
        inline void Raise(const HWND window) {
            // AttachThreadInput needs a message queue on this thread, and nothing has made one yet
            MSG message;
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

            const DWORD front = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const DWORD self = GetCurrentThreadId();
            const bool attached = front && front != self && AttachThreadInput(self, front, TRUE);

            BringWindowToTop(window);
            SetForegroundWindow(window);

            if (attached) {
                AttachThreadInput(self, front, FALSE);
            }
        }
    }

    /// What the user could switch to right now, so a launch can tell afterwards what is new.
    inline std::vector<HWND> Switchable() {
        std::vector<HWND> windows;

        EnumWindows([](const HWND window, const LPARAM param) -> BOOL {
            if (IsWindowVisible(window) && !GetWindow(window, GW_OWNER)
                && !(GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
                && GetWindowTextLengthW(window)) {
                reinterpret_cast<std::vector<HWND>*>(param)->push_back(window);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));

        return windows;
    }

    /**
     * @brief Brings up the window the launch produced, when it did not come up on its own.
     *
     * An app that was already running gets the foreground from nobody - nothing started it, so
     * its own request is refused and Windows flashes its taskbar button instead. That is what a
     * terminal asked for a second window does, and raising it from here is the only way a hotkey
     * launcher can put a window it did not create in front.
     *
     * Gives up as soon as the foreground moves anywhere at all, so a user who switches away mid
     * launch is never yanked back, and an app that came up by itself is left alone.
     */
    inline void RaiseNew(const std::vector<HWND>& before, const HWND wasInFront) {
        constexpr int Attempts = 30;   // a cold app can take a few seconds to show a window
        constexpr DWORD StepMs = 100;

        for (int attempt = 0; attempt < Attempts; attempt++) {
            Sleep(StepMs);

            if (GetForegroundWindow() != wasInFront) {
                return;
            }

            for (const HWND window : Switchable()) {
                if (std::ranges::find(before, window) == before.end()) {
                    Detail::Raise(window);
                    return;
                }
            }
        }
    }

    /**
     * @brief Runs the command as the logged on user.
     *
     * The shell reports a bad command to the user itself, so unlike ShellExecuteEx there is
     * nothing to log here - success only says the request reached explorer.exe.
     *
     * @return false when the shell is out of reach: no desktop yet, or explorer restarting.
     */
    inline bool Run(const std::wstring& file, const std::wstring& params) {
        const ComPtr<IShellDispatch2> shell = Detail::DesktopShell();
        if (!shell) {
            return false;
        }

        // Empty verb and directory mean the shell's own defaults, the same ones a double click
        // would use
        return SUCCEEDED(shell->ShellExecuteW(_bstr_t(file.c_str()),
                                              _variant_t(params.c_str()),
                                              _variant_t(),
                                              _variant_t(),
                                              _variant_t(SW_SHOWNORMAL)));
    }
}

