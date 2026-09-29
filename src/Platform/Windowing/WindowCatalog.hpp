#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace WindowCatalog {
    /// What alt-tab would list, on any desktop - a window on another one is only shell-cloaked.
    bool IsAppWindow(HWND window);
    /// Top to bottom.
    std::vector<HWND> AppWindows();
    std::vector<HWND> ForeignAppWindows();
    /// Image name, lowercase - "chrome.exe". Empty when the process cannot be opened.
    std::wstring ExeName(HWND window);
    std::vector<std::wstring> AppNames();
}
