#pragma once

#include <algorithm>
#include <vector>
#include <windows.h>

/**
 * @brief One page of the settings window.
 *
 * A page is a DIALOGEX template plus the code that fills it in. The window knows neither: it
 * walks the list below, puts the template in its frame and hands the page its own handle.
 */
struct SettingsPage {
    const wchar_t* Title;
    /// DIALOGEX template id from Resource.h.
    int TemplateId;
    /// Fills the page in, every time it is opened. The page is destroyed and rebuilt on each
    /// visit, so nothing may be cached across calls without checking it is still alive.
    void (*Build)(HWND page);
    /// Where the page sits in the sidebar - see the keys below.
    int Order = 100;
};

/**
 * @brief Every settings page anyone has registered, in the order the sidebar shows them.
 *
 * Registration order alone would put About wherever its owner happens to sit in main, so the
 * frame's own first and last pages pin themselves with a sort key and everything else keeps the
 * order it registered in.
 */
namespace SettingsHost {

    /// A module leaves Order alone unless it has a reason to be first or last.
    inline constexpr int First = 0;
    inline constexpr int Last = 1000;

    inline std::vector<SettingsPage> Pages;

    inline void AddPage(const SettingsPage& page) {
        Pages.insert(std::ranges::upper_bound(Pages, page.Order, {}, &SettingsPage::Order), page);
    }
}
