#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace WindowCatalog {
    bool IsAppWindow(HWND window);
    std::vector<HWND> AppWindows();
    std::vector<std::wstring> AppNames();
}
