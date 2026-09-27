#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "Autocorrect.hpp"
#include "Platform/Foreground.hpp"

namespace KeyboardExclusions {
    inline std::vector<std::string> Parse(std::string_view csv) {
        std::vector<std::string> apps;
        for (size_t start = 0; start < csv.size();) {
            const size_t comma = csv.find(',', start);
            std::string exe = Foreground::CanonicalApp(csv.substr(start, comma - start));
            if (!exe.empty()) apps.push_back(std::move(exe));
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
        std::ranges::sort(apps);
        apps.erase(std::ranges::unique(apps).begin(), apps.end());
        return apps;
    }

    inline std::string Join(const std::vector<std::string>& apps) {
        std::string csv;
        for (const auto& app : apps) {
            if (!csv.empty()) csv += ", ";
            csv += app;
        }
        return csv;
    }

    /// Input thread: typing in `window` is tracked. Decided once per window, not before the snapshot catches up.
    class Filter {
    public:
        bool Allowed(const Autocorrect::Runtime* runtime, const HWND window) {
            if (!runtime || !window) return false;
            if (!runtime->Auto && runtime->Excluded.empty() && !runtime->SkipFullscreen) return true;
            if (window != _window) {
                _window = window;
                _status = Status::Unknown;
            }
            if (_status == Status::Unknown) {
                const auto foreground = Foreground::Current();
                if (foreground && foreground->Window == window && !foreground->Exe.empty()) {
                    _status = runtime->Excluded.contains(foreground->Exe)
                              || (runtime->SkipFullscreen && foreground->Fullscreen) ? Status::Excluded : Status::Allowed;
                }
            }
            return _status == Status::Allowed;
        }

    private:
        enum class Status : uint8_t { Unknown, Allowed, Excluded };
        HWND _window = nullptr;
        Status _status = Status::Unknown;
    };
}
