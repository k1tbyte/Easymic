#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

#include "OrderedInsert.hpp"

/// UI thread only. The icon is one slot the user gives to a provider; every provider's menu item is listed.
struct TrayProvider {
    std::string_view Id;
    const wchar_t* Title = L"";
    int Order = 100;
    /// Null: the provider is a menu contribution only and is not offered for the icon.
    HICON (*Icon)() = nullptr;
    std::wstring (*Tooltip)() = nullptr;
    void (*ThemeChanged)() = nullptr;
    const wchar_t* (*MenuLabel)() = nullptr;
    void (*MenuInvoke)() = nullptr;
};

namespace Tray {

    inline constexpr int Last = 1000;

    inline std::vector<TrayProvider> Providers;

    inline void Add(const TrayProvider& provider) {
        InsertByOrder(Providers, provider);
    }

    /// Any thread: the UI side posts.
    inline void (*Refresh)() = nullptr;

    inline void Changed() {
        if (Refresh) {
            Refresh();
        }
    }

    inline std::vector<const TrayProvider*> Choices() {
        std::vector<const TrayProvider*> choices;
        for (const TrayProvider& provider : Providers) {
            if (provider.Icon) {
                choices.push_back(&provider);
            }
        }
        return choices;
    }

    /// Empty (nobody chose yet) takes the first by Order; an unknown Id falls back to the last, the app icon.
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
