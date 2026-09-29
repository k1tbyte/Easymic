#include "RulesPage.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>
#include <vector>

#include "../Autocorrect/UserRules.hpp"
#include "Core/AppConfig.hpp"
#include "Platform/Controls.hpp"
#include "Platform/Str.hpp"
#include "RuleDialog.hpp"

namespace {

    enum : int { IdList = 300, IdAdd, IdEdit, IdRemove };
    constexpr wchar_t PanelClass[] = L"EasyLauncher.KeyboardRules";

    AppConfig* _config = nullptr;

    std::vector<WordRule>& _rules() { return _config->Keyboard.Learned.Rules; }

    int _selected(HWND panel) {
        return ListView_GetNextItem(GetDlgItem(panel, IdList), -1, LVNI_SELECTED);
    }

    void _syncButtons(HWND panel) {
        const BOOL selected = _selected(panel) >= 0;
        EnableWindow(GetDlgItem(panel, IdEdit), selected);
        EnableWindow(GetDlgItem(panel, IdRemove), selected);
    }

    void _fill(HWND panel, const int select = -1) {
        const HWND list = GetDlgItem(panel, IdList);
        ListView_DeleteAllItems(list);
        for (int i = 0; i < static_cast<int>(_rules().size()); ++i) {
            const WordRule& rule = _rules()[i];
            std::wstring text = Str::Utf8ToWide(rule.Text);
            LVITEMW item{.mask = LVIF_TEXT, .iItem = i, .pszText = text.data()};
            ListView_InsertItem(list, &item);
            ListView_SetItemText(list, i, 1, const_cast<wchar_t*>(rule.Match == WordMatch::Exact ? L"Whole word"
                                 : rule.Match == WordMatch::StartsWith ? L"Starts with" : L"Contains"));
            ListView_SetItemText(list, i, 2, const_cast<wchar_t*>(rule.CaseSensitive ? L"Match" : L"Ignore"));
            ListView_SetItemText(list, i, 3, const_cast<wchar_t*>(rule.Always ? L"Convert" : L"Never convert"));
        }
        Controls::FitColumns(list, {40, 25, 15});
        if (select >= 0) {
            ListView_SetItemState(list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, select, FALSE);
        }
        _syncButtons(panel);
    }

    void _edit(HWND panel, const int row) {
        const bool existing = row >= 0 && row < static_cast<int>(_rules().size());
        WordRule rule = existing ? _rules()[row] : WordRule{};
        if (!RuleDialog::Show(panel, rule) || !UserRules::Put(_config->Keyboard.Learned, rule,
                                                               existing ? row : static_cast<size_t>(-1))) {
            return;
        }
        const auto it = std::ranges::find(_rules(), rule);
        _fill(panel, static_cast<int>(it - _rules().begin()));
    }

    void _remove(HWND panel) {
        const int row = _selected(panel);
        if (row >= 0 && row < static_cast<int>(_rules().size())) {
            _rules().erase(_rules().begin() + row);
            _fill(panel, std::min(row, static_cast<int>(_rules().size()) - 1));
        }
    }

    LRESULT CALLBACK _proc(HWND panel, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                if (HIWORD(wParam) == BN_CLICKED) {
                    switch (LOWORD(wParam)) {
                        case IdAdd: _edit(panel, -1); break;
                        case IdEdit: _edit(panel, _selected(panel)); break;
                        case IdRemove: _remove(panel); break;
                        default: break;
                    }
                }
                return 0;
            case WM_NOTIFY:
                if (const auto* header = reinterpret_cast<const NMHDR*>(lParam); header->idFrom == IdList) {
                    if (header->code == NM_DBLCLK) {
                        _edit(panel, _selected(panel));
                    } else if (header->code == LVN_ITEMCHANGED) {
                        _syncButtons(panel);
                    }
                }
                return 0;
            default:
                return DefWindowProcW(panel, message, wParam, lParam);
        }
    }
}

void RulesPage::Create(HWND page, const RECT& cell, const int id, AppConfig& config) {
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

    add(WC_STATICW, L"Words as typed in the wrong layout", SS_LEFT, {0, 0, width, 8}, -1);
    const HWND list = add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER
                          | WS_TABSTOP, {0, 11, width, height - 18}, IdList, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    for (const wchar_t* title : {L"Pattern", L"Condition", L"Case", L"Action"}) {
        LVCOLUMNW column{.mask = LVCF_TEXT, .pszText = const_cast<wchar_t*>(title)};
        ListView_InsertColumn(list, Header_GetItemCount(ListView_GetHeader(list)), &column);
    }
    add(WC_BUTTONW, L"Add...", BS_PUSHBUTTON | WS_TABSTOP, {0, height - 14, 50, height}, IdAdd);
    add(WC_BUTTONW, L"Edit...", BS_PUSHBUTTON | WS_TABSTOP, {54, height - 14, 104, height}, IdEdit);
    add(WC_BUTTONW, L"Remove", BS_PUSHBUTTON | WS_TABSTOP, {108, height - 14, 158, height}, IdRemove);
    _fill(panel);
}
