#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

/**
 * @brief Something that can paint the tray icon, put an item in the tray menu, or both.
 *
 * The icon is one slot and the menu is a list, so the two are separate claims: the user picks who
 * gets the icon, and every contribution gets its menu item. Plain function pointers, like
 * OverlayLayer, and every one of them runs on the UI thread.
 */
struct TrayProvider {
    /// Stored in the config, so permanent.
    std::string_view Id;
    /// The radio label on the Tray page.
    const wchar_t* Title = L"";
    int Order = 100;
    /// What the icon should be right now; null keeps whatever is up. Without one the provider is a
    /// menu contribution only and is not on the radio.
    HICON (*Icon)() = nullptr;
    /// Null falls back to APP_NAME.
    std::wstring (*Tooltip)() = nullptr;
    /// Themed copies are built on the first switch to a light taskbar, so a provider wants telling.
    void (*ThemeChanged)() = nullptr;
    /// A function because it reports state - "Enable bell sound" and "Disable bell sound" are one item.
    const wchar_t* (*MenuLabel)() = nullptr;
    void (*MenuInvoke)() = nullptr;
};

namespace Tray {

    /// The frame's app icon pins itself here, which also makes it the fallback for a stale choice.
    inline constexpr int Last = 1000;

    /// Registered at startup and never removed, so an index is an identity for the process lifetime.
    inline std::vector<TrayProvider> Providers;

    inline void Add(const TrayProvider& provider) {
        Providers.insert(std::ranges::upper_bound(Providers, provider.Order, {}, &TrayProvider::Order), provider);
    }

    /// Set by the window at bind time. Callable from any thread - the UI side posts.
    inline void (*Refresh)() = nullptr;

    inline void Changed() {
        if (Refresh) {
            Refresh();
        }
    }

    /// The providers that can own the icon, in the order the radio lists them.
    inline std::vector<const TrayProvider*> Choices() {
        std::vector<const TrayProvider*> choices;
        for (const TrayProvider& provider : Providers) {
            if (provider.Icon) {
                choices.push_back(&provider);
            }
        }
        return choices;
    }

    /**
     * @brief Who paints the icon for a stored choice.
     *
     * Empty is a config nobody has chosen in yet, and takes the first by Order - sort order may
     * pick the default, never overrule a choice. An Id no module registered takes the last, the
     * app icon, the way an unparseable hotkey leaves its binding visible rather than dropping it.
     */
    inline const TrayProvider* Owner(const std::string_view id) {
        const std::vector<const TrayProvider*> choices = Choices();
        if (choices.empty()) {
            return nullptr;
        }
        if (id.empty()) {
            return choices.front();
        }

        const auto match = std::ranges::find(choices, id, &TrayProvider::Id);
        return match != choices.end() ? *match : choices.back();
    }
}
