#include "WindowCatalog.hpp"

#include <set>

#include <dwmapi.h>

#include "Foreground.hpp"

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

    std::vector<std::wstring> AppNames() {
        const DWORD self = GetCurrentProcessId();
        std::set<std::wstring> unique;
        for (const HWND window : AppWindows()) {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid != self) {
                if (auto name = Foreground::ExeName(window); !name.empty()) {
                    unique.insert(std::move(name));
                }
            }
        }
        return {unique.begin(), unique.end()};
    }
}
