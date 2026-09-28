#pragma once

#include <windows.h>

namespace UIAccess {
    HWND GetOrCreateWindow(const char* key, DWORD exStyle, DWORD style);
    bool InjectDisplayAffinity(HWND hWnd, DWORD affinity);
}
