#include "ShellContext.hpp"

#include <exdisp.h>
#include <servprov.h>
#include <shlobj.h>

#include "definitions.h"

namespace ShellContext {

namespace {

    bool _hasClass(const HWND window, const wchar_t* name) {
        wchar_t className[32];
        return GetClassNameW(window, className, ARRAYSIZE(className))
               && wcscmp(className, name) == 0;
    }

    /// Every tab is its own IShellWindows entry under one HWND, in an order that does not follow the tab strip:
    /// the topmost ShellTabWindowClass child is the active one. FindWindowEx does not see these children, hence the walk.
    HWND _activeTab(const HWND window) {
        HWND active = nullptr;

        EnumChildWindows(window, [](const HWND child, const LPARAM param) -> BOOL {
            if (!_hasClass(child, L"ShellTabWindowClass")) {
                return TRUE;
            }
            *reinterpret_cast<HWND*>(param) = child;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&active));

        return active;
    }

    std::wstring _folderOf(const ComPtr<IShellBrowser>& browser) {
        ComPtr<IShellView> view;
        ComPtr<IFolderView> folderView;
        ComPtr<IPersistFolder2> folder;

        if (FAILED(browser->QueryActiveShellView(&view)) || FAILED(view.As(&folderView))
            || FAILED(folderView->GetFolder(IID_PPV_ARGS(&folder)))) {
            return {};
        }

        PIDLIST_ABSOLUTE id = nullptr;
        if (FAILED(folder->GetCurFolder(&id)) || !id) {
            return {};
        }

        // This PC and Control Panel have no path
        wchar_t path[MAX_PATH];
        const bool real = SHGetPathFromIDListW(id, path) != FALSE;
        CoTaskMemFree(id);

        return real ? std::wstring(path) : std::wstring{};
    }

} // anonymous namespace

std::wstring ActiveFolder(const HWND window) {
    if (!_hasClass(window, L"CabinetWClass")) {
        return {};
    }

    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&windows)))) {
        return {};
    }

    const HWND tab = _activeTab(window);

    long count = 0;
    windows->get_Count(&count);

    for (long i = 0; i < count; i++) {
        VARIANT index;
        index.vt = VT_I4;
        index.lVal = i;

        ComPtr<IDispatch> entry;
        if (FAILED(windows->Item(index, &entry)) || !entry) {
            continue;
        }

        ComPtr<IWebBrowser2> browser;
        SHANDLE_PTR owner = 0;
        if (FAILED(entry.As(&browser)) || FAILED(browser->get_HWND(&owner))
            || reinterpret_cast<HWND>(owner) != window) {
            continue;
        }

        ComPtr<IServiceProvider> provider;
        ComPtr<IShellBrowser> shellBrowser;
        if (FAILED(browser.As(&provider))
            || FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser)))) {
            continue;
        }

        HWND entryTab = nullptr;
        shellBrowser->GetWindow(&entryTab);
        if (tab && entryTab != tab) {
            continue;
        }

        return _folderOf(shellBrowser);
    }

    return {};
}

}
