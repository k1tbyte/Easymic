#ifndef EASYMIC_SHELLCONTEXT_HPP
#define EASYMIC_SHELLCONTEXT_HPP

#include <windows.h>

#include <exdisp.h>
#include <servprov.h>
#include <shlobj.h>
#include <string>

#include "definitions.h"

/**
 * @brief What the window in front is looking at.
 *
 * Explorer publishes this over COM only, so a lookup is a handful of calls into explorer.exe and
 * costs about 3 ms - fine once per hotkey, never on a path that runs unconditionally.
 */
namespace ShellContext {

    namespace Detail {
        inline bool HasClass(const HWND window, const wchar_t* name) {
            wchar_t className[32];
            return GetClassNameW(window, className, ARRAYSIZE(className))
                   && wcscmp(className, name) == 0;
        }

        /**
         * @brief The tab the user is looking at.
         *
         * Every tab of a window is its own IShellWindows entry under the same HWND, and their
         * order there does not follow the tab strip - matching on the HWND alone picks a tab at
         * random. The tab window on top of the z-order is the active one, and
         * IShellBrowser::GetWindow says which tab an entry belongs to.
         *
         * FindWindowEx does not see these despite them being direct children, hence the walk.
         */
        inline HWND ActiveTab(const HWND window) {
            HWND active = nullptr;

            EnumChildWindows(window, [](const HWND child, const LPARAM param) -> BOOL {
                if (!HasClass(child, L"ShellTabWindowClass")) {
                    return TRUE;
                }
                *reinterpret_cast<HWND*>(param) = child;
                return FALSE;
            }, reinterpret_cast<LPARAM>(&active));

            return active;
        }

        inline std::wstring FolderOf(const ComPtr<IShellBrowser>& browser) {
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

            // This PC and Control Panel are folders with no path - nothing a command could take
            wchar_t path[MAX_PATH];
            const bool real = SHGetPathFromIDListW(id, path) != FALSE;
            CoTaskMemFree(id);

            return real ? std::wstring(path) : std::wstring{};
        }
    }

    /**
     * @brief The folder the active Explorer tab shows, empty when there is none.
     *
     * Apartment threaded, and the caller owns CoInitialize - this runs on the command thread,
     * which is in an apartment already because ShellExecuteEx needs one.
     */
    inline std::wstring ActiveFolder(const HWND window) {
        if (!Detail::HasClass(window, L"CabinetWClass")) {
            return {};
        }

        ComPtr<IShellWindows> windows;
        if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&windows)))) {
            return {};
        }

        const HWND tab = Detail::ActiveTab(window);

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

            return Detail::FolderOf(shellBrowser);
        }

        return {};
    }
}

#endif //EASYMIC_SHELLCONTEXT_HPP
