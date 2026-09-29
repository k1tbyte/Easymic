#include "ExcludedApps.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>

#include "../KeyboardExclusions.hpp"
#include "Core/AppConfig.hpp"
#include "Platform/Controls.hpp"
#include "Platform/Foreground.hpp"
#include "Platform/Str.hpp"
#include "Platform/WindowCatalog.hpp"

namespace {

    enum : int { IdList = 200, IdCombo, IdAdd, IdRemove };
    constexpr wchar_t PanelClass[] = L"EasyLauncher.KeyboardExclusions";

    AppConfig* _config = nullptr;

    void _syncButtons(HWND panel) {
        const int selected = ListView_GetNextItem(GetDlgItem(panel, IdList), -1, LVNI_SELECTED);
        EnableWindow(GetDlgItem(panel, IdRemove), selected >= 0);
    }

    void _fill(HWND panel) {
        const HWND list = GetDlgItem(panel, IdList);
        ListView_DeleteAllItems(list);
        const auto apps = KeyboardExclusions::Parse(_config->Keyboard.Exclude);
        for (int i = 0; i < static_cast<int>(apps.size()); ++i) {
            std::wstring wide = Str::Utf8ToWide(apps[i]);
            LVITEMW item{.mask = LVIF_TEXT, .iItem = i, .pszText = wide.data()};
            ListView_InsertItem(list, &item);
        }
        Controls::FitColumns(list, {});
        _syncButtons(panel);
    }

    void _fillCombo(const HWND combo) {
        const std::wstring text = Controls::Text(combo);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto& name : WindowCatalog::AppNames()) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        SetWindowTextW(combo, text.c_str());
    }

    void _add(HWND panel) {
        const HWND combo = GetDlgItem(panel, IdCombo);
        std::string exe = Foreground::CanonicalApp(Str::WideToUtf8(Controls::Text(combo)));
        auto apps = KeyboardExclusions::Parse(_config->Keyboard.Exclude);
        if (exe.empty() || std::ranges::contains(apps, exe)) {
            return;
        }
        apps.push_back(std::move(exe));
        std::ranges::sort(apps);
        _config->Keyboard.Exclude = KeyboardExclusions::Join(apps);
        _fill(panel);
        SetWindowTextW(combo, L"");
    }

    void _remove(HWND panel) {
        const int row = ListView_GetNextItem(GetDlgItem(panel, IdList), -1, LVNI_SELECTED);
        auto apps = KeyboardExclusions::Parse(_config->Keyboard.Exclude);
        if (row < 0 || row >= static_cast<int>(apps.size())) {
            return;
        }
        apps.erase(apps.begin() + row);
        _config->Keyboard.Exclude = KeyboardExclusions::Join(apps);
        _fill(panel);
    }

    LRESULT CALLBACK _proc(HWND panel, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                if (LOWORD(wParam) == IdCombo && HIWORD(wParam) == CBN_DROPDOWN) {
                    _fillCombo(GetDlgItem(panel, IdCombo));
                } else if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IdAdd) {
                    _add(panel);
                } else if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IdRemove) {
                    _remove(panel);
                }
                return 0;
            case WM_NOTIFY:
                if (const auto* header = reinterpret_cast<const NMHDR*>(lParam);
                    header->idFrom == IdList && header->code == LVN_ITEMCHANGED) {
                    _syncButtons(panel);
                }
                return 0;
            default:
                return DefWindowProcW(panel, message, wParam, lParam);
        }
    }
}

void ExcludedApps::Create(HWND page, const RECT& cell, const int id, AppConfig& config) {
    const HWND panel = Controls::Panel(page, cell, id, PanelClass, _proc);
    if (!panel) {
        return;
    }

    _config = &config;
    const int width = cell.right - cell.left, height = cell.bottom - cell.top;
    const auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style, const RECT& at, const int childId,
                         const DWORD exStyle = 0) {
        return Controls::Create(page, panel, cls, text, style, at, childId, exStyle);
    };

    add(WC_STATICW, L"No conversion in these apps (.exe)", SS_LEFT, {0, 0, width, 8}, -1);
    const HWND list = add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER
                          | WS_TABSTOP, {0, 11, width, height - 18}, IdList, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    LVCOLUMNW column{};
    ListView_InsertColumn(list, 0, &column);
    add(WC_COMBOBOXW, L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP | WS_VSCROLL,
        {0, height - 14, width - 108, height + 70}, IdCombo);
    add(WC_BUTTONW, L"Add", BS_PUSHBUTTON | WS_TABSTOP, {width - 104, height - 14, width - 54, height}, IdAdd);
    add(WC_BUTTONW, L"Remove", BS_PUSHBUTTON | WS_TABSTOP, {width - 50, height - 14, width, height}, IdRemove);
    _fill(panel);
}
