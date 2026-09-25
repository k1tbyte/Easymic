#include "WindowCatalog.hpp"

#include <dwmapi.h>

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
}
