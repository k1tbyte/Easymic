#pragma once

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
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

    namespace Detail {
        /**
         * @brief Every layout the user has, in the order the language bar cycles them.
         *
         * Preload rather than GetKeyboardLayoutList: that one only reports what the session has
         * already loaded, so a layout nobody has typed in since logon is missing from it and
         * switching to it would quietly do nothing. Loading one that is already loaded just
         * hands back its handle, and without KLF_ACTIVATE nothing is switched by asking.
         */
        inline std::vector<HKL> Installed() {
            std::vector<HKL> layouts;
            HKEY preload = nullptr;

            if (RegOpenKeyExW(HKEY_CURRENT_USER, LR"(Keyboard Layout\Preload)", 0, KEY_READ,
                              &preload) == ERROR_SUCCESS) {
                for (int index = 1;; index++) {
                    wchar_t identifier[KL_NAMELENGTH];
                    DWORD size = sizeof(identifier);
                    if (RegQueryValueExW(preload, std::to_wstring(index).c_str(), nullptr, nullptr,
                                         reinterpret_cast<LPBYTE>(identifier), &size) != ERROR_SUCCESS) {
                        break;
                    }

                    if (const HKL layout = LoadKeyboardLayoutW(identifier, KLF_NOTELLSHELL)) {
                        layouts.push_back(layout);
                    }
                }
                RegCloseKey(preload);
            }

            if (layouts.empty()) {
                layouts.resize(GetKeyboardLayoutList(0, nullptr));
                layouts.resize(GetKeyboardLayoutList(static_cast<int>(layouts.size()), layouts.data()));
            }

            return layouts;
        }

        /**
         * @brief Whether one token of the locale list names this layout.
         *
         * A name is matched as a prefix, so "en" takes en-US and "en-GB" only that one. Hex is
         * the escape hatch: four digits are a language id, eight the whole layout handle, which
         * is the only way to tell two layouts of the same language apart.
         */
        inline bool Matches(const std::string_view token, const HKL layout) {
            const auto handle = static_cast<uint32_t>(reinterpret_cast<UINT_PTR>(layout));

            if (token.size() == 4 || token.size() == 8) {
                uint32_t value = 0;
                if (std::from_chars(token.data(), token.data() + token.size(), value, 16).ec == std::errc{}) {
                    return token.size() == 8 ? value == handle : value == LOWORD(handle);
                }
            }

            wchar_t name[LOCALE_NAME_MAX_LENGTH];
            if (!LCIDToLocaleName(MAKELCID(LOWORD(handle), SORT_DEFAULT), name,
                                  LOCALE_NAME_MAX_LENGTH, 0)) {
                return false;
            }

            // Locale names are ASCII, so this compares in place instead of widening the token
            for (size_t i = 0; i < token.size(); i++) {
                if (!name[i] || towlower(name[i]) != towlower(static_cast<wchar_t>(token[i]))) {
                    return false;
                }
            }
            return true;
        }

        /// The layouts the list names, in its order, skipping what is not installed. An empty
        /// list means every installed layout, which is the plain "next layout" behaviour.
        inline std::vector<HKL> Ring(const std::string_view locales) {
            const std::vector<HKL> installed = Installed();
            if (locales.find_first_not_of(" \t,") == std::string_view::npos) {
                return installed;
            }

            std::vector<HKL> ring;
            for (size_t start = 0; start < locales.size();) {
                const size_t comma = locales.find(',', start);
                std::string_view token = locales.substr(start, comma - start);
                start = comma == std::string_view::npos ? locales.size() : comma + 1;

                while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
                while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
                if (token.empty()) {
                    continue;
                }

                // First match wins: two layouts of one language must not both join the ring, or
                // a pair of locales would cycle three ways
                for (const HKL layout : installed) {
                    if (Matches(token, layout)) {
                        ring.push_back(layout);
                        break;
                    }
                }
            }

            return ring;
        }
    }

    /**
     * @brief Moves the focused window to the next layout of the ring the locale list describes.
     *
     * A layout outside the ring means the ring was not what the user was typing in, so the first
     * entry is where they asked to end up - which is also what makes a one entry list a plain
     * "switch to this one".
     *
     * @return false when nothing has focus or nothing in the list is installed.
     */
    inline bool SwitchNext(const std::string_view locales = {}) {
        const HWND target = FocusedWindow();
        if (!target) {
            return false;
        }

        const std::vector<HKL> ring = Detail::Ring(locales);
        if (ring.empty()) {
            return false;
        }

        const HKL current = GetKeyboardLayout(GetWindowThreadProcessId(target, nullptr));
        const size_t at = std::ranges::find(ring, current) - ring.begin();
        const HKL next = ring[at + 1 < ring.size() ? at + 1 : 0];

        // The flag alone is enough for a plain Win32 window, but a TSF app reads the handle, so
        // both are sent and the ignored half costs nothing.
        return PostMessageW(target, WM_INPUTLANGCHANGEREQUEST,
                            INPUTLANGCHANGE_FORWARD, reinterpret_cast<LPARAM>(next));
    }
}

