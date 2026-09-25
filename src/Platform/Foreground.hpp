#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace Foreground {

    struct Snapshot {
        HWND Window = nullptr;
        std::string Exe;
    };

    bool Start();
    void Stop();

    const Snapshot& CurrentOnUi();

    std::wstring ExeName(HWND window);
    std::string CanonicalApp(std::string_view exe);
}
