#pragma once

#include <windows.h>

#include <string>

namespace ShellContext {

    /// Empty when the window shows no folder. About 3 ms of COM; the caller owns CoInitialize (apartment threaded).
    std::wstring ActiveFolder(HWND window);
}
