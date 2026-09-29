#include "WindowCatalog.hpp"

#include <set>

#include <dwmapi.h>

#include "Str.hpp"

namespace WindowCatalog {
    bool IsAppWindow(const HWND window) {
        if (!IsWindowVisible(window) || window == GetShellWindow()) {
            return false;
        }

        const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_APPWINDOW)
            && (GetWindow(window, GW_OWNER) || (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))) {
            return false;
        }

        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        return !(cloaked & DWM_CLOAKED_APP);
    }

    std::vector<HWND> AppWindows() {
        std::vector<HWND> windows;
        EnumWindows([](const HWND window, const LPARAM param) -> BOOL {
            if (IsAppWindow(window)) {
                reinterpret_cast<std::vector<HWND>*>(param)->push_back(window);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));
        return windows;
    }

    std::wstring ExeName(const HWND window) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};

        wchar_t path[1024];
        DWORD size = std::size(path);
        const BOOL found = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        if (!found) return {};

        const std::wstring_view full(path, size);
        return Str::Lower(full.substr(full.find_last_of(L'\\') + 1));
    }

    std::vector<std::wstring> AppNames() {
        const DWORD self = GetCurrentProcessId();
        std::set<std::wstring> unique;
        for (const HWND window : AppWindows()) {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid != self) {
                if (auto name = ExeName(window); !name.empty()) {
                    unique.insert(std::move(name));
                }
            }
        }
        return {unique.begin(), unique.end()};
    }
}
