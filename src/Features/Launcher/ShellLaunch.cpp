#include "ShellLaunch.hpp"

#include <comutil.h>
#include <exdisp.h>
#include <servprov.h>
#include <shldisp.h>
#include <shlobj.h>

#include <algorithm>

#include "Windowing/WindowCatalog.hpp"
#include "definitions.h"

namespace ShellLaunch {

namespace {

    constexpr DWORD GiveUpAfterMs = 3000;
    constexpr DWORD StepMs = 100;

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

    /// Sharing the foreground window's input queue makes the request count as coming from it. Both calls are
    /// needed: BringWindowToTop does the activating, SetForegroundWindow alone is still refused.
    void _raise(const HWND window) {
        // AttachThreadInput needs a message queue on this thread
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

void RaiseNew(const std::vector<HWND>& before, const HWND wasInFront) {
    for (DWORD waited = 0; waited < GiveUpAfterMs; waited += StepMs) {
        Sleep(StepMs);

        if (GetForegroundWindow() != wasInFront) {
            return;
        }

        for (const HWND window : WindowCatalog::AppWindows()) {
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

    return SUCCEEDED(shell->ShellExecuteW(_bstr_t(file.c_str()),
                                          _variant_t(params.c_str()),
                                          _variant_t(),
                                          _variant_t(),
                                          _variant_t(SW_SHOWNORMAL)));
}

}
