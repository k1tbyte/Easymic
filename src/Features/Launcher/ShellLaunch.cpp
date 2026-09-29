#include "ShellLaunch.hpp"

#include <comutil.h>
#include <exdisp.h>
#include <servprov.h>
#include <shldisp.h>
#include <shlobj.h>

#include <algorithm>

#include "definitions.h"

namespace ShellLaunch {

namespace {

    /// The desktop's shell view - the part of explorer.exe that can run something for us.
    ComPtr<IShellDispatch2> _desktopShell() {
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
    void _raise(const HWND window) {
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

} // anonymous namespace

std::vector<HWND> Switchable() {
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

void RaiseNew(const std::vector<HWND>& before, const HWND wasInFront) {
    constexpr int Attempts = 30;   // a cold app can take a few seconds to show a window
    constexpr DWORD StepMs = 100;

    for (int attempt = 0; attempt < Attempts; attempt++) {
        Sleep(StepMs);

        if (GetForegroundWindow() != wasInFront) {
            return;
        }

        for (const HWND window : Switchable()) {
            if (std::ranges::find(before, window) == before.end()) {
                _raise(window);
                return;
            }
        }
    }
}

bool Run(const std::wstring& file, const std::wstring& params) {
    const ComPtr<IShellDispatch2> shell = _desktopShell();
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
