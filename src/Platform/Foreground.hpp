#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <windows.h>

namespace Foreground {

    struct Snapshot {
        HWND Window = nullptr;
        std::string Exe;
        /// Covered its whole monitor when it came to the foreground; resolved with Exe
        bool Fullscreen = false;
    };

    bool Start();
    void Stop();

    /// Any thread, without allocating - the input thread reads it from inside the hook. Null
    /// before Start.
    std::shared_ptr<const Snapshot> Current();

    std::wstring ExeName(HWND window);
    std::string CanonicalApp(std::string_view exe);
}
