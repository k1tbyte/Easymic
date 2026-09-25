#pragma once

#include <vector>
#include <windows.h>

namespace WindowCatalog {
    bool IsAppWindow(HWND window);
    std::vector<HWND> AppWindows();
}
