#include "RuleDialog.hpp"

#include "../Autocorrect/UserRules.hpp"
#include "Core/AppConfig.hpp"
#include "Platform/Controls.hpp"
#include "Platform/Str.hpp"
#include "Resources/Resource.h"

namespace {

    void _updateNote(HWND dialog) {
        const bool contains = IsDlgButtonChecked(dialog, IDC_RULE_MATCH_CONTAINS) == BST_CHECKED;
        const bool never = IsDlgButtonChecked(dialog, IDC_RULE_NEVER) == BST_CHECKED;
        // A later key could still form the pattern, so no word may switch before its Space
        SetDlgItemTextW(dialog, IDC_RULE_NOTE, contains && never
            ? L"While typing mode: with a Contains + Never rule every word waits for its Space." : L"");
    }

    void _save(HWND dialog, WordRule& result) {
        WordRule rule{.Text = Str::WideToUtf8(Controls::Text(GetDlgItem(dialog, IDC_RULE_WORD))),
                      .Match = IsDlgButtonChecked(dialog, IDC_RULE_MATCH_EXACT) == BST_CHECKED ? WordMatch::Exact
                             : IsDlgButtonChecked(dialog, IDC_RULE_MATCH_STARTS) == BST_CHECKED ? WordMatch::StartsWith
                             : WordMatch::Contains,
                      .CaseSensitive = IsDlgButtonChecked(dialog, IDC_RULE_CASE) == BST_CHECKED,
                      .Always = IsDlgButtonChecked(dialog, IDC_RULE_ALWAYS) == BST_CHECKED};
        if (!UserRules::Normalize(rule)) {
            MessageBoxW(dialog, L"Enter one nonempty word or pattern, up to 64 characters, without spaces.",
                         L"Invalid rule", MB_OK | MB_ICONERROR);
            return;
        }
        result = std::move(rule);
        EndDialog(dialog, IDOK);
    }

    INT_PTR CALLBACK _proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* rule = reinterpret_cast<WordRule*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
        switch (message) {
            case WM_INITDIALOG: {
                rule = reinterpret_cast<WordRule*>(lParam);
                SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
                SetWindowTextW(dialog, rule->Text.empty() ? L"New autocorrect rule" : L"Edit autocorrect rule");
                SetDlgItemTextW(dialog, IDC_RULE_WORD, Str::Utf8ToWide(rule->Text).c_str());
                const int match = rule->Match == WordMatch::Exact ? IDC_RULE_MATCH_EXACT
                                  : rule->Match == WordMatch::StartsWith ? IDC_RULE_MATCH_STARTS
                                  : IDC_RULE_MATCH_CONTAINS;
                CheckRadioButton(dialog, IDC_RULE_MATCH_EXACT, IDC_RULE_MATCH_CONTAINS, match);
                CheckDlgButton(dialog, IDC_RULE_CASE, rule->CaseSensitive ? BST_CHECKED : BST_UNCHECKED);
                CheckRadioButton(dialog, IDC_RULE_ALWAYS, IDC_RULE_NEVER, rule->Always ? IDC_RULE_ALWAYS : IDC_RULE_NEVER);
                _updateNote(dialog);
                RECT owner, window;
                GetWindowRect(GetParent(dialog), &owner);
                GetWindowRect(dialog, &window);
                SetWindowPos(dialog, nullptr,
                             owner.left + (owner.right - owner.left - window.right + window.left) / 2,
                             owner.top + (owner.bottom - owner.top - window.bottom + window.top) / 2,
                             0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                return TRUE;
            }
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case IDOK: _save(dialog, *rule); return TRUE;
                    case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
                    case IDC_RULE_MATCH_EXACT:
                    case IDC_RULE_MATCH_STARTS:
                    case IDC_RULE_MATCH_CONTAINS:
                    case IDC_RULE_ALWAYS:
                    case IDC_RULE_NEVER: _updateNote(dialog); return TRUE;
                }
                break;
        }
        return FALSE;
    }
}

bool RuleDialog::Show(HWND owner, WordRule& rule) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RULE_EDIT), GetAncestor(owner, GA_ROOT),
                           _proc, reinterpret_cast<LPARAM>(&rule)) == IDOK;
}
