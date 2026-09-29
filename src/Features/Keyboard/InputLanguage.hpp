#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Keyboard layout switching for the window the user is actually typing in.
 *
 * The request goes to the focused window alone, never to HWND_BROADCAST: a layout is per thread,
 * so broadcasting rewrites it for every app on the desktop and races with whatever they were
 * doing. This matches the shell hotkey under "let me use a different input method for each app
 * window"; under the system-wide setting the shell also moves the other apps, which is what this
 * deliberately does not do.
 */
namespace InputLanguage {

    /// Where keystrokes land right now, which is a child of the foreground window in most apps.
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

    /// A layout is per thread, so this is the one keystrokes into the window are read with.
    inline HKL LayoutOf(const HWND window) {
        return GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr));
    }

    struct Layout {
        HKL Handle;
        /// KLID, "00000409" - what the config keeps. The handle in hex for one only the session list knew.
        std::string Id;
    };

    /**
     * @brief Every layout the user has, in the order the language bar cycles them.
     *
     * Preload first: GetKeyboardLayoutList only reports what the session has already loaded, so
     * a layout nobody has typed in since logon is missing from it and switching to it would
     * quietly do nothing. Loading one that is already loaded just hands back its handle, and
     * without KLF_ACTIVATE nothing is switched by asking. The session list follows, for what
     * the Settings app added without touching Preload.
     */
    std::vector<Layout> Installed();

    /// The configured KLID's place in the list; an empty one takes the fallback-th layout. -1 when
    /// neither is there.
    int Find(const std::vector<Layout>& layouts, std::string_view id, size_t fallback);

    /// "Russian - ru-RU": the layout's own name, which tells two layouts of one language apart.
    std::wstring Title(const Layout& layout);

    /// No INPUTLANGCHANGE_FORWARD: the handle alone names where to go.
    inline bool SwitchTo(const HWND target, const HKL layout) {
        return PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layout));
    }

    /**
     * @brief Moves the focused window to the next layout of the ring the locale list describes.
     *
     * A layout outside the ring means the ring was not what the user was typing in, so the first
     * entry is where they asked to end up - which is also what makes a one entry list a plain
     * "switch to this one".
     *
     * @return the layout asked for, null when nothing has focus, nothing in the list is installed
     * or the window refused the post.
     */
    HKL SwitchNext(std::string_view locales = {});
}
