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
        /// Covered its whole monitor when it came to the foreground; resolved with Exe
        bool Fullscreen = false;
    };

    /// `toUi` runs a task on the UI thread, where the snapshot is written.
    bool Start(void (*toUi)(std::function<void()>));
    void Stop();

    /// Any thread, without allocating - the input thread reads it from inside the hook. Null
    /// before Start.
    std::shared_ptr<const Snapshot> Current();

    std::string CanonicalApp(std::string_view exe);
}
