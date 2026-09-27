#pragma once

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

#include "Str.hpp"

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
    inline std::vector<Layout> Installed() {
        std::vector<Layout> layouts;
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
                    layouts.push_back({layout, Str::WideToUtf8(identifier)});
                }
            }
            RegCloseKey(preload);
        }

        std::vector<HKL> handles(GetKeyboardLayoutList(0, nullptr));
        handles.resize(GetKeyboardLayoutList(static_cast<int>(handles.size()), handles.data()));
        for (const HKL handle : handles) {
            if (std::ranges::find(layouts, handle, &Layout::Handle) == layouts.end()) {
                // No KLID names it (a French HKL with a US keyboard shares 00000409): the handle does
                char id[9]{};
                std::snprintf(id, sizeof id, "%08X", static_cast<unsigned>(reinterpret_cast<UINT_PTR>(handle)));
                layouts.push_back({handle, id});
            }
        }

        return layouts;
    }

    /// The configured KLID's place in the list; an empty one takes the fallback-th layout. -1 when
    /// neither is there.
    inline int Find(const std::vector<Layout>& layouts, const std::string_view id, const size_t fallback) {
        if (id.empty()) {
            return fallback < layouts.size() ? static_cast<int>(fallback) : -1;
        }
        const auto it = std::ranges::find(layouts, id, &Layout::Id);
        return it == layouts.end() ? -1 : static_cast<int>(it - layouts.begin());
    }

    /// "Russian - ru-RU": the layout's own name, which tells two layouts of one language apart.
    inline std::wstring Title(const Layout& layout) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        LCIDToLocaleName(MAKELCID(LOWORD(reinterpret_cast<UINT_PTR>(layout.Handle)), SORT_DEFAULT), locale,
                         LOCALE_NAME_MAX_LENGTH, 0);
        wchar_t name[128]{};
        DWORD size = sizeof(name);
        const std::wstring key = LR"(SYSTEM\CurrentControlSet\Control\Keyboard Layouts\)" + Str::Utf8ToWide(layout.Id);
        if (layout.Id.empty() || RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Layout Text", RRF_RT_REG_SZ,
                                              nullptr, name, &size) != ERROR_SUCCESS) {
            return locale;
        }
        return std::wstring{name} + L" - " + locale;
    }

    /// No INPUTLANGCHANGE_FORWARD: the handle alone names where to go.
    inline bool SwitchTo(const HWND target, const HKL layout) {
        return PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layout));
    }

    namespace Detail {

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
            std::vector<HKL> installed;
            for (const Layout& layout : Installed()) {
                installed.push_back(layout.Handle);
            }
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
     * @return the layout asked for, null when nothing has focus, nothing in the list is installed
     * or the window refused the post.
     */
    inline HKL SwitchNext(const std::string_view locales = {}) {
        const HWND target = FocusedWindow();
        if (!target) {
            return nullptr;
        }

        const std::vector<HKL> ring = Detail::Ring(locales);
        if (ring.empty()) {
            return nullptr;
        }

        const size_t at = std::ranges::find(ring, LayoutOf(target)) - ring.begin();
        const HKL next = ring[at + 1 < ring.size() ? at + 1 : 0];

        // The flag alone is enough for a plain Win32 window, but a TSF app reads the handle, so
        // both are sent and the ignored half costs nothing.
        return PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_FORWARD,
                            reinterpret_cast<LPARAM>(next)) ? next : nullptr;
    }
}

