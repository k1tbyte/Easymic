#pragma once

#include <algorithm>

#include "Features/Keyboard/Autocorrect/Autocorrect.hpp"
#include "Platform/Windowing/Foreground.hpp"

namespace Autocorrect {

    /// Input thread: typing in `window` is tracked. Decided once per window, not before the snapshot catches up.
    class AppFilter {
    public:
        bool Allowed(const Runtime* runtime, const HWND window) {
            if (!runtime || !window) {
                return false;
            }
            if (!runtime->Auto && runtime->Excluded.empty() && !runtime->SkipFullscreen) {
                return true;
            }
            if (window != _window) {
                _window = window;
                _status = Status::Unknown;
            }
            if (_status == Status::Unknown) {
                _resolve(*runtime, window);
            }
            return _status == Status::Allowed;
        }

    private:
        enum class Status : uint8_t { Unknown, Allowed, Excluded };

        void _resolve(const Runtime& runtime, const HWND window) {
            const auto foreground = Foreground::Current();
            if (!foreground || foreground->Window != window || foreground->Exe.empty()) {
                return;
            }
            const bool excluded = std::ranges::binary_search(runtime.Excluded, foreground->Exe)
                                  || (runtime.SkipFullscreen && foreground->Fullscreen);
            _status = excluded ? Status::Excluded : Status::Allowed;
        }

        HWND _window = nullptr;
        Status _status = Status::Unknown;
    };
}
