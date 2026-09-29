#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include "../Check.hpp"
#include "Core/AppConfig.hpp"
#include "Core/SettingsHost.hpp"
#include "Features/Keyboard/KeyboardExclusions.hpp"
#include "Features/Keyboard/Settings/KeyboardPage.hpp"
#include "Platform/Str.hpp"
#include "Resources/Resource.h"
#include "UI/Settings/SettingsWindow.hpp"

namespace {
    using Test::Check;

    int _saves = 0;
    int _commits = 0;
    int _skipped = 0;
    std::filesystem::path _screenshots;

    constexpr int FirstControlId = 1000;
    constexpr int ControlIdsPerRow = 16;
    enum GeneralRow { ShowLayoutRow, PairARow, PackARow, PairBRow, PackBRow, AutoCorrectRow, SkipPasswordsRow,
                      SkipFullscreenRow, FrequencyRow, ThresholdRow };

    constexpr int _rowId(const GeneralRow row) { return FirstControlId + row * ControlIdsPerRow; }

    HWND _child(HWND parent, const wchar_t* cls, const wchar_t* text = nullptr) {
        struct Search { const wchar_t* Class; const wchar_t* Text; HWND Found = nullptr; } search{cls, text};
        EnumChildWindows(parent, [](HWND hwnd, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            wchar_t cls[80]{}, text[256]{};
            GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
            GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
            if (!wcscmp(cls, search.Class) && (!search.Text || !wcscmp(text, search.Text))) {
                search.Found = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.Found;
    }

    void _click(HWND button) {
        Check(button != nullptr, "button exists");
        SendMessageW(GetParent(button), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED),
                     reinterpret_cast<LPARAM>(button));
    }

    HWND _tab(HWND settings, const int index) {
        const HWND tabs = _child(settings, WC_TABCONTROLW);
        Check(tabs && TabCtrl_GetItemCount(tabs) == 3, "Keyboard has three tabs");
        TabCtrl_SetCurSel(tabs, index);
        NMHDR notify{tabs, static_cast<UINT_PTR>(GetDlgCtrlID(tabs)), TCN_SELCHANGE};
        SendMessageW(GetParent(tabs), WM_NOTIFY, notify.idFrom, reinterpret_cast<LPARAM>(&notify));
        return FindWindowExW(GetParent(tabs), nullptr, L"#32770", nullptr);
    }

    std::wstring _cell(HWND list, const int row, const int column) {
        wchar_t text[256]{};
        ListView_GetItemText(list, row, column, text, static_cast<int>(std::size(text)));
        return text;
    }

    void _listFits(HWND page, HWND list) {
        RECT body, rect, client;
        GetClientRect(page, &body);
        GetWindowRect(list, &rect);
        GetClientRect(list, &client);
        Check(rect.bottom - rect.top > body.bottom * 2 / 3, "list uses most of the tab height");
        Check(!(GetWindowLongPtrW(page, GWL_STYLE) & WS_VSCROLL), "list tab has no outer scrollbar");
        int width = 0;
        for (int i = 0; i < Header_GetItemCount(ListView_GetHeader(list)); ++i) {
            width += ListView_GetColumnWidth(list, i);
        }
        Check(width <= client.right, "columns fit after the vertical scrollbar appears");
        Check(!(GetWindowLongPtrW(list, GWL_STYLE) & WS_HSCROLL), "list has no horizontal scrollbar");
    }

    void (*_onDialog)(HWND) = nullptr;

    /// Answers the rule dialog from inside its own modal loop.
    void CALLBACK _answerDialog(HWND, UINT, const UINT_PTR timer, DWORD) {
        HWND dialog = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM found) -> BOOL {
            if (!GetDlgItem(hwnd, IDC_RULE_WORD)) {
                return TRUE;
            }
            *reinterpret_cast<HWND*>(found) = hwnd;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&dialog));
        if (dialog) {
            KillTimer(nullptr, timer);
            _onDialog(dialog);
        }
    }

    void _withDialog(HWND button, void (*answer)(HWND)) {
        _onDialog = answer;
        SetTimer(nullptr, 0, 20, _answerDialog);
        _click(button);
    }

    void _pick(HWND dialog, const int id) {
        SendMessageW(GetDlgItem(dialog, id), BM_CLICK, 0, 0);
    }

    std::wstring _comboItem(HWND combo, const int index) {
        wchar_t text[256]{};
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
        return text;
    }

    void _choose(HWND page, const int id, const int index) {
        const HWND combo = GetDlgItem(page, id);
        SendMessageW(combo, CB_SETCURSEL, index, 0);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
    }

    void _packs(HWND page) {
        const int pairA = _rowId(PairARow), packAId = _rowId(PackARow);
        const HWND packA = GetDlgItem(page, packAId), packB = GetDlgItem(page, _rowId(PackBRow));
        if (_comboItem(packA, 1) != L"en.pack" || _comboItem(packB, 1) != L"ru.pack") {
            ++_skipped;
            std::puts("SKIPPED pack checks: they need en.pack and ru.pack for the first two layouts");
            return;
        }
        _choose(page, packAId, 1);
        _choose(page, pairA, 1);
        Check(_comboItem(packA, 1) == L"ru.pack", "the pack list follows the layout");
        Check(_comboItem(packA, 2) == L"Missing or incompatible: en.pack", "an incompatible pack stays visible");
        Check(!IsWindowEnabled(GetDlgItem(page, _rowId(AutoCorrectRow))), "autocorrect needs compatible packs");
        _choose(page, pairA, 0);
        Check(SendMessageW(packA, CB_GETCURSEL, 0, 0) == 1, "the pack survives a layout round trip");
    }

    void _select(HWND list, const int row) {
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    void _capture(HWND window, const wchar_t* name) {
        if (_screenshots.empty()) {
            return;
        }
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_ERASE);
        RECT rect;
        GetWindowRect(window, &rect);
        const HDC dc = GetDC(window), memory = CreateCompatibleDC(dc);
        const HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right - rect.left, rect.bottom - rect.top);
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        Check(PrintWindow(window, memory, 0) != FALSE, "PrintWindow captures the private desktop window");
        SelectObject(memory, previous);
        {
            Gdiplus::Bitmap image(bitmap, nullptr);
            const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
            Check(image.Save((_screenshots / name).c_str(), &png) == Gdiplus::Ok, "save screenshot");
        }
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(window, dc);
    }

    void _exclusions() {
        const auto apps = KeyboardExclusions::Parse(" Notepad.EXE ,C:\\Tools\\chrome.exe,, notepad.exe,");
        Check(apps == std::vector<std::string>{"chrome.exe", "notepad.exe"}, "exclusions are canonical, sorted and unique");
        Check(KeyboardExclusions::Parse(" , ,").empty(), "blank exclusions are dropped");
        Check(KeyboardExclusions::Join(apps) == "chrome.exe, notepad.exe", "exclusions join into the config text");
    }

    void _run() {
        _exclusions();
        Gdiplus::GdiplusStartupInput startup;
        ULONG_PTR gdiplus = 0;
        Gdiplus::GdiplusStartup(&gdiplus, &startup, nullptr);
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        AppConfig cfg;
        for (int i = 0; i < 40; ++i) {
            if (i) {
                cfg.Keyboard.Exclude += ", ";
            }
            cfg.Keyboard.Exclude += "app" + std::to_string(i) + ".exe";
            cfg.Keyboard.Learned.Rules.push_back({.Text = "word" + std::to_string(i)});
        }
        cfg.Keyboard.Learned.Rules.push_back({.Text = ",he["});
        const AppConfig original = cfg;
        KeyboardPage::Register(cfg);
        static constexpr SettingsRow commitRows[] = {
            {.Kind = RowKind::Text, .Commit = [](HWND, AppConfig&, const AppConfig&) { ++_commits; }},
        };
        static constexpr SettingsTab commitTabs[] = {{L"First", commitRows}, {L"Second", commitRows}};
        SettingsHost::AddPage({.Title = L"Commit test", .Tabs = commitTabs});

        {
            SettingsWindow window(GetModuleHandleW(nullptr), nullptr, cfg);
            window.Show();
            const HWND settings = window.GetHandle();
            Check(settings != nullptr, "settings window created");
            if (!settings) {
                return;
            }
            HWND page = _tab(settings, 0);
            const HWND showLayout = GetDlgItem(page, _rowId(ShowLayoutRow));
            Check(showLayout != nullptr, "General still uses declarative controls");
            SendMessageW(showLayout, BM_SETCHECK, BST_CHECKED, 0);
            _click(showLayout);
            Check(cfg.Keyboard.ShowLayout, "General checkbox updates config");
            _capture(settings, L"general.png");
            const HWND frequency = GetDlgItem(page, _rowId(FrequencyRow)), threshold = GetDlgItem(page, _rowId(ThresholdRow));
            Check(IsWindowEnabled(threshold), "threshold is on with frequency analysis");
            SendMessageW(frequency, BM_SETCHECK, BST_UNCHECKED, 0);
            _click(frequency);
            Check(!cfg.Keyboard.FrequencyAnalysis && !IsWindowEnabled(threshold), "threshold follows frequency analysis");
            _packs(page);

            page = _tab(settings, 1);
            HWND list = _child(page, WC_LISTVIEWW);
            Check(ListView_GetItemCount(list) == 40, "all excluded apps are listed");
            _listFits(page, list);
            const HWND combo = _child(page, WC_COMBOBOXW);
            SetWindowTextW(combo, L"EDITOR.EXE");
            _click(_child(page, WC_BUTTONW, L"Add"));
            Check(ListView_GetItemCount(list) == 41 && cfg.Keyboard.Exclude.contains("editor.exe"),
                   "app addition canonicalizes the executable");
            SetWindowTextW(combo, L"editor.exe");
            _click(_child(page, WC_BUTTONW, L"Add"));
            Check(ListView_GetItemCount(list) == 41, "duplicate app is not added");
            _capture(settings, L"excluded-apps.png");

            page = _tab(settings, 2);
            list = _child(page, WC_LISTVIEWW);
            Check(ListView_GetItemCount(list) == 41, "all user rules are listed");
            Check(Header_GetItemCount(ListView_GetHeader(list)) == 4, "rules have four columns");
            Check(_cell(list, 40, 0) == L",he[", "rule list preserves punctuation");
            _listFits(page, list);
            _capture(settings, L"rules.png");

            const HWND edit = _child(page, WC_BUTTONW, L"Edit...");
            Check(!IsWindowEnabled(edit), "Edit waits for a selection");
            _withDialog(_child(page, WC_BUTTONW, L"Add..."), [](HWND dialog) {
                Check(IsDlgButtonChecked(dialog, IDC_RULE_MATCH_EXACT) && IsDlgButtonChecked(dialog, IDC_RULE_ALWAYS)
                       && !IsDlgButtonChecked(dialog, IDC_RULE_CASE), "a new rule starts as whole word, any case, convert");
                SetDlgItemTextW(dialog, IDC_RULE_WORD, L"  Gh  ");
                _pick(dialog, IDC_RULE_MATCH_CONTAINS);
                _pick(dialog, IDC_RULE_CASE);
                _pick(dialog, IDC_RULE_NEVER);
                wchar_t note[128]{};
                GetDlgItemTextW(dialog, IDC_RULE_NOTE, note, static_cast<int>(std::size(note)));
                Check(note[0] != 0, "Contains + Never explains the wait");
                _capture(dialog, L"rule-dialog.png");
                SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            });
            const WordRule added{.Text = "Gh", .Match = WordMatch::Contains, .CaseSensitive = true, .Always = false};
            Check(cfg.Keyboard.Learned.Rules.back() == added, "Add saves the trimmed rule with its options");
            Check(ListView_GetItemCount(list) == 42 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 41,
                   "the new rule is listed and selected");

            _select(list, 40);
            Check(IsWindowEnabled(edit), "a selection enables Edit");
            _withDialog(edit, [](HWND dialog) {
                wchar_t text[16]{};
                GetDlgItemTextW(dialog, IDC_RULE_WORD, text, static_cast<int>(std::size(text)));
                Check(!wcscmp(text, L",he["), "Edit shows the selected rule");
                SetDlgItemTextW(dialog, IDC_RULE_WORD, L"changed");
                SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            });
            Check(cfg.Keyboard.Learned.Rules[40].Text == ",he[", "Cancel in the dialog keeps the rule");

            _select(list, 0);
            _withDialog(edit, [](HWND dialog) {
                SetDlgItemTextW(dialog, IDC_RULE_WORD, L"ofc");
                _pick(dialog, IDC_RULE_NEVER);
                SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            });
            Check(cfg.Keyboard.Learned.Rules[0] == WordRule{.Text = "ofc", .Always = false} && _cell(list, 0, 0) == L"ofc"
                   && _cell(list, 0, 3) == L"Never convert", "Edit replaces the row in place");

            _click(_child(page, WC_BUTTONW, L"Remove"));
            Check(ListView_GetItemCount(list) == 41 && cfg.Keyboard.Learned.Rules[0].Text == "word1", "Remove drops the row");

            page = _tab(settings, 0);
            Check(SendMessageW(GetDlgItem(page, _rowId(ShowLayoutRow)), BM_GETCHECK, 0, 0) == BST_CHECKED,
                   "General changes survive switching tabs");
            SendMessageW(settings, WM_COMMAND, IDCANCEL, 0);
            Check(cfg == original && _saves == 0 && _commits == 0, "Cancel restores changes from every tab");
        }
        {
            SettingsWindow window(GetModuleHandleW(nullptr), nullptr, cfg);
            window.Show();
            cfg.Keyboard.ShowLayout = true;
            _tab(window.GetHandle(), 2);
            SendMessageW(window.GetHandle(), WM_COMMAND, IDOK, 0);
            Check(cfg.Keyboard.ShowLayout && _saves == 1, "OK keeps and saves changes");
            Check(_commits == 2, "OK commits even tabs that were never opened");
        }
        SettingsHost::Pages.clear();
        Gdiplus::GdiplusShutdown(gdiplus);
    }
}

void AppConfig::Save() const { ++_saves; }
namespace SettingsPages { void Open() {} void Close() {} }
namespace UAC { bool IsElevated() { return false; } }

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--screenshots") {
        _screenshots = std::filesystem::temp_directory_path()
                       / (L"easylauncher-keyboard-ui-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directory(_screenshots);
    }
    const std::wstring name = L"EasyLauncher.UiTest." + std::to_wstring(GetCurrentProcessId());
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
                                         DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    if (!desktop) {
        std::printf("Cannot create private test desktop: %lu\n", GetLastError());
        return 1;
    }
    std::thread test([desktop] {
        if (!SetThreadDesktop(desktop)) {
            Check(false, "attach test thread to its private desktop");
            return;
        }
        _run();
    });
    test.join();
    CloseDesktop(desktop);
    if (!_screenshots.empty()) {
        std::printf("Screenshots: %s\n", Str::WideToUtf8(_screenshots.wstring()).c_str());
    }
    if (Test::Failures) {
        return 1;
    }
    if (_skipped) {
        std::printf("keyboard UI checks passed, %d group(s) SKIPPED (see above)\n", _skipped);
    } else {
        std::puts("keyboard UI checks passed (private desktop, no input injection)");
    }
}
