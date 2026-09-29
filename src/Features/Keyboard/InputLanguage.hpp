#pragma once

#include <windows.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Switching aims at the focused window alone: HWND_BROADCAST would rewrite the per-thread layout of every app.
namespace InputLanguage {

    inline HWND FocusedWindow() {
        const HWND foreground = GetForegroundWindow();
        if (!foreground) {
            return nullptr;
        }

        GUITHREADINFO info{.cbSize = sizeof(GUITHREADINFO)};
        if (GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info) && info.hwndFocus) {
            return info.hwndFocus;
        }
        return foreground;
    }

    inline HKL LayoutOf(const HWND window) {
        return GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr));
    }

    struct Layout {
        HKL Handle;
        /// KLID ("00000409"), what the config keeps; the handle in hex for one only the session list knew.
        std::string Id;

        bool operator==(const Layout&) const = default;
    };

    /// In the order the language bar cycles them.
    std::vector<Layout> Installed();

    /// An empty `id` takes the fallback-th layout. -1 when neither is there.
    int Find(const std::vector<Layout>& layouts, std::string_view id, size_t fallback);

    std::wstring Title(const Layout& layout);

    std::string Iso639(HKL layout);

    /// No INPUTLANGCHANGE_FORWARD: the handle alone names where to go.
    inline bool SwitchTo(const HWND target, const HKL layout) {
        return PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layout));
    }

    /// The installed layouts a comma separated locale list names, in its order; an empty list names all.
    /// A token is a locale name prefix ("en" takes en-US), or hex: 4 digits a language id, 8 the handle.
    std::vector<HKL> Ring(std::string_view locales);

    /// The layout after the focused window's own in `ring`, the first when it is outside. Null when nothing
    /// switched: no focus, an empty ring or a refused post.
    HKL SwitchNext(std::span<const HKL> ring);
}
