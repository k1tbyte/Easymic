#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <windows.h>

namespace Foreground {

    struct Snapshot {
        HWND Window = nullptr;
        std::string Exe;
        bool Fullscreen = false;
    };

    bool Start(void (*toUi)(std::function<void()>));
    void Stop();

    /// Any thread, without allocating: the input thread reads it inside the hook. Null before Start.
    std::shared_ptr<const Snapshot> Current();

    std::string CanonicalApp(std::string_view exe);
}
