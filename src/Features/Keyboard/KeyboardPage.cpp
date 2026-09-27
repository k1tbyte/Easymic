#include "KeyboardPage.hpp"

#include "AppConfig.hpp"
#include "Controls.hpp"
#include "Core/Overlay.hpp"
#include "Core/SettingsHost.hpp"
#include "InputLanguage.hpp"
#include "KeyboardExclusions.hpp"
#include "KeyboardPacks.hpp"
#include "Learning.hpp"
#include "Platform/Foreground.hpp"
#include "Platform/WindowCatalog.hpp"
#include "Str.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>
#include <vector>

namespace {

    enum : int { IdList = 200, IdCombo, IdAdd, IdRemove, IdWords, IdWord, IdAlways, IdNever, IdForget };

    constexpr wchar_t PanelClass[] = L"EasyLauncher.KeyboardLists";
    constexpr int PanelHeight = 168;

    AppConfig* _config = nullptr;
    HWND _panel = nullptr;
    bool _pageReady = false;
    std::vector<KeyboardPacks::PackInfo> _discovered;
    std::string _shownPairA, _shownPairB, _shownPackA, _shownPackB;
    bool _defaultA = false, _defaultB = false;

    HWND _item(const int id) { return GetDlgItem(_panel, id); }

    std::wstring _text(const HWND control) {
        std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
        text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
        return text;
    }

    void _fillList() {
        HWND list = _item(IdList);
        ListView_DeleteAllItems(list);
        auto apps = KeyboardExclusions::Parse(_config->Keyboard.Exclude);
        for (int i = 0; i < static_cast<int>(apps.size()); ++i) {
            std::wstring wide = Str::Utf8ToWide(apps[i]);
            LVITEMW item{.mask = LVIF_TEXT, .iItem = i, .pszText = wide.data()};
            ListView_InsertItem(list, &item);
        }
    }

    void _fillCombo() {
        HWND combo = _item(IdCombo);
        const std::wstring text = _text(combo);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto& name : WindowCatalog::AppNames()) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        SetWindowTextW(combo, text.c_str());
    }

    void _addApp() {
        std::string exe = Foreground::CanonicalApp(Str::WideToUtf8(_text(_item(IdCombo))));
        if (exe.empty()) return;
        auto apps = KeyboardExclusions::Parse(_config->Keyboard.Exclude);
        if (std::ranges::find(apps, exe) != apps.end()) return;
        apps.push_back(std::move(exe));
        std::ranges::sort(apps);
        _config->Keyboard.Exclude = KeyboardExclusions::Join(apps);
        _fillList();
        SetWindowTextW(_item(IdCombo), L"");
    }

    void _removeApp() {
        const int row = ListView_GetNextItem(_item(IdList), -1, LVNI_SELECTED);
        auto apps = KeyboardExclusions::Parse(_config->Keyboard.Exclude);
        if (row < 0 || row >= static_cast<int>(apps.size())) return;
        apps.erase(apps.begin() + row);
        _config->Keyboard.Exclude = KeyboardExclusions::Join(apps);
        _fillList();
    }

    void _fillWords() {
        HWND list = _item(IdWords);
        ListView_DeleteAllItems(list);
        const LearnedWords& learned = _config->Keyboard.Learned;
        int row = 0;
        for (const bool always : {true, false}) {
            for (const auto& word : always ? learned.Always : learned.Never) {
                std::wstring wide = Str::Utf8ToWide(word);
                LVITEMW item{.mask = LVIF_TEXT, .iItem = row, .pszText = wide.data()};
                ListView_InsertItem(list, &item);
                ListView_SetItemText(list, row++, 1, const_cast<wchar_t*>(always ? L"always" : L"never"));
            }
        }
    }

    void _teach(const bool always) {
        if (!Learning::File(_config->Keyboard.Learned, _text(_item(IdWord)), always).empty()) {
            _fillWords();
            SetWindowTextW(_item(IdWord), L"");
        }
    }

    void _forget() {
        LearnedWords& learned = _config->Keyboard.Learned;
        const int row = ListView_GetNextItem(_item(IdWords), -1, LVNI_SELECTED);
        const int always = static_cast<int>(learned.Always.size());
        if (row < 0) return;
        if (row < always) {
            learned.Always.erase(learned.Always.begin() + row);
        } else if (row - always < static_cast<int>(learned.Never.size())) {
            learned.Never.erase(learned.Never.begin() + (row - always));
        }
        _fillWords();
    }

    void _command(const int id, const int code) {
        if (id == IdCombo && code == CBN_DROPDOWN) {
            _fillCombo();
            return;
        }
        if (code != BN_CLICKED) return;
        switch (id) {
            case IdAdd:    _addApp(); break;
            case IdRemove: _removeApp(); break;
            case IdAlways: _teach(true); break;
            case IdNever:  _teach(false); break;
            case IdForget: _forget(); break;
            default: break;
        }
    }

    LRESULT CALLBACK _proc(const HWND hwnd, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                _command(LOWORD(wParam), HIWORD(wParam));
                return 0;
            case WM_DESTROY:
                _panel = nullptr;
                _pageReady = false;
                break;
            default: break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void _createListsPanel(const HWND page, const RECT& cell, const int id) {
        static const bool registered = [] {
            const WNDCLASSW wc{.lpfnWndProc = _proc,
                               .hInstance = GetModuleHandleW(nullptr),
                               .hCursor = LoadCursorW(nullptr, IDC_ARROW),
                               .hbrBackground = GetSysColorBrush(COLOR_BTNFACE),
                               .lpszClassName = PanelClass};
            return RegisterClassW(&wc) != 0;
        }();
        if (!registered) return;

        _panel = Controls::Create(page, page, PanelClass, L"", WS_CLIPCHILDREN, cell, id, WS_EX_CONTROLPARENT);
        const int width = cell.right - cell.left;
        const auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style, const RECT& at,
                             const int childId, const DWORD exStyle = 0) {
            return Controls::Create(page, _panel, cls, text, style, at, childId, exStyle);
        };

        // Columns split the list's width: the first gets `share` percent, the second the rest
        const auto list = [&](const int top, const int listId, const int share) {
            const HWND view = add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS
                                  | LVS_NOSORTHEADER | LVS_NOCOLUMNHEADER | WS_TABSTOP,
                                  {0, top, width, top + 50}, listId, WS_EX_CLIENTEDGE);
            ListView_SetExtendedListViewStyle(view, LVS_EX_FULLROWSELECT);
            RECT client;
            GetClientRect(view, &client);
            LVCOLUMNW column{.mask = LVCF_WIDTH, .cx = client.right * share / 100};
            ListView_InsertColumn(view, 0, &column);
            if (share < 100) {
                column.cx = client.right - column.cx;
                ListView_InsertColumn(view, 1, &column);
            }
        };

        add(WC_STATICW, L"Excluded from conversion (.exe)", SS_LEFT, {0, 0, width, 8}, -1);
        list(11, IdList, 100);
        add(WC_COMBOBOXW, L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP | WS_VSCROLL,
            {0, 65, width - 90, 135}, IdCombo);
        add(WC_BUTTONW, L"Add", BS_PUSHBUTTON | WS_TABSTOP, {width - 86, 65, width - 46, 79}, IdAdd);
        add(WC_BUTTONW, L"Remove", BS_PUSHBUTTON | WS_TABSTOP, {width - 42, 65, width, 79}, IdRemove);

        add(WC_STATICW, L"Learned words: convert always, or never", SS_LEFT, {0, 89, width, 97}, -1);
        list(100, IdWords, 75);
        add(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, {0, 154, width - 134, 168}, IdWord, WS_EX_CLIENTEDGE);
        add(WC_BUTTONW, L"Always", BS_PUSHBUTTON | WS_TABSTOP, {width - 130, 154, width - 90, 168}, IdAlways);
        add(WC_BUTTONW, L"Never", BS_PUSHBUTTON | WS_TABSTOP, {width - 86, 154, width - 46, 168}, IdNever);
        add(WC_BUTTONW, L"Remove", BS_PUSHBUTTON | WS_TABSTOP, {width - 42, 154, width, 168}, IdForget);

        _fillList();
        _fillCombo();
        _fillWords();
    }

    std::vector<InputLanguage::Layout> _layouts;
    std::vector<std::wstring> _titles;
    std::vector<const wchar_t*> _items;
    std::vector<KeyboardPacks::PackInfo> _packsA, _packsB;

    void _refreshPacks(const AppConfig& cfg);

    std::span<const wchar_t* const> _layoutItems() {
        if (!_pageReady) {
            _layouts = InputLanguage::Installed();
            _titles.clear();
            for (const auto& layout : _layouts) {
                _titles.push_back(InputLanguage::Title(layout));
            }
            _items.clear();
            for (const auto& title : _titles) _items.push_back(title.c_str());
            _discovered = KeyboardPacks::Discover();
            _refreshPacks(*_config);
            _pageReady = true;
        } else if (_config->Keyboard.PairA != _shownPairA || _config->Keyboard.PairB != _shownPairB
                   || _config->Keyboard.PackA != _shownPackA || _config->Keyboard.PackB != _shownPackB) {
            _refreshPacks(*_config);
        }
        return _items;
    }

    int _findPack(const std::vector<KeyboardPacks::PackInfo>& packs, const std::string& packName);

    bool _canToggleAutoCorrect(const AppConfig& cfg) {
        const int a = InputLanguage::Find(_layouts, cfg.Keyboard.PairA, 0);
        const int b = InputLanguage::Find(_layouts, cfg.Keyboard.PairB, 1);
        const bool readyA = cfg.Keyboard.PackA.empty() ? _defaultA
            : _findPack(_packsA, cfg.Keyboard.PackA) <= static_cast<int>(_packsA.size());
        const bool readyB = cfg.Keyboard.PackB.empty() ? _defaultB
            : _findPack(_packsB, cfg.Keyboard.PackB) <= static_cast<int>(_packsB.size());
        return cfg.Keyboard.AutoCorrect || (a >= 0 && b >= 0 && a != b
               && _layouts[a].Handle != _layouts[b].Handle && readyA && readyB);
    }

    template <std::string KeyboardSettings::*Field, size_t Fallback>
    constexpr RowField PairField() {
        return {.Get = [](const AppConfig& cfg) { return InputLanguage::Find(_layouts, cfg.Keyboard.*Field, Fallback); },
                .Set = [](AppConfig& cfg, const int index) {
                    if (index >= 0 && index < static_cast<int>(_layouts.size())) {
                        cfg.Keyboard.*Field = _layouts[index].Id;
                    }
                }};
    }

    // Pack combos per side
    std::vector<std::wstring> _packTitlesA, _packTitlesB;
    std::vector<const wchar_t*> _packItemsA, _packItemsB;

    int _findPack(const std::vector<KeyboardPacks::PackInfo>& packs, const std::string& packName) {
        if (packName.empty()) return 0;
        const std::wstring wide = Str::Utf8ToWide(packName);
        for (size_t i = 0; i < packs.size(); ++i) {
            if (packs[i].Filename == wide) return static_cast<int>(i + 1);
        }
        return static_cast<int>(packs.size() + 1);
    }

    void _buildPackItems(const std::vector<KeyboardPacks::PackInfo>& packs, const HKL layout,
                         const bool available, const std::string& selected,
                         std::vector<std::wstring>& titles, std::vector<const wchar_t*>& items) {
        titles.clear();
        items.clear();
        const std::wstring name = layout ? Str::Utf8ToWide(KeyboardPacks::Locale(layout)) + L".pack" : L"";
        titles.push_back(layout ? L"Auto: " + name + (available ? L"" : L" (missing)") : L"Choose a layout");
        for (const auto& pack : packs) titles.push_back(pack.Filename);
        if (!selected.empty() && _findPack(packs, selected) > static_cast<int>(packs.size())) {
            titles.push_back(L"Missing or incompatible: " + Str::Utf8ToWide(selected));
        }
        for (const auto& title : titles) items.push_back(title.c_str());
    }

    void _refreshPacks(const AppConfig& cfg) {
        const int a = InputLanguage::Find(_layouts, cfg.Keyboard.PairA, 0);
        const int b = InputLanguage::Find(_layouts, cfg.Keyboard.PairB, 1);
        const auto compatible = [&](const int index) {
            std::vector<KeyboardPacks::PackInfo> result;
            if (index < 0) return result;
            const std::string locale = KeyboardPacks::Locale(_layouts[index].Handle);
            for (const auto& pack : _discovered) {
                if (pack.Valid && pack.EmbeddedLocale == locale) result.push_back(pack);
            }
            return result;
        };
        _packsA = compatible(a);
        _packsB = compatible(b);
        // Auto is `<locale>.pack`, one of the compatible
        const auto hasAuto = [&](const int index, const std::vector<KeyboardPacks::PackInfo>& packs) {
            return index >= 0 && _findPack(packs, KeyboardPacks::Locale(_layouts[index].Handle) + ".pack")
                                     <= static_cast<int>(packs.size());
        };
        _defaultA = hasAuto(a, _packsA);
        _defaultB = hasAuto(b, _packsB);
        _buildPackItems(_packsA, a >= 0 ? _layouts[a].Handle : nullptr, _defaultA,
                        cfg.Keyboard.PackA, _packTitlesA, _packItemsA);
        _buildPackItems(_packsB, b >= 0 ? _layouts[b].Handle : nullptr, _defaultB,
                        cfg.Keyboard.PackB, _packTitlesB, _packItemsB);
        _shownPairA = cfg.Keyboard.PairA;
        _shownPairB = cfg.Keyboard.PairB;
        _shownPackA = cfg.Keyboard.PackA;
        _shownPackB = cfg.Keyboard.PackB;
    }

    std::span<const wchar_t* const> _packItemsAFn() { return _packItemsA; }
    std::span<const wchar_t* const> _packItemsBFn() { return _packItemsB; }

    template <std::string KeyboardSettings::*Field, std::vector<KeyboardPacks::PackInfo>* Packs>
    constexpr RowField PackField() {
        return {.Get = [](const AppConfig& cfg) { return _findPack(*Packs, cfg.Keyboard.*Field); },
                .Set = [](AppConfig& cfg, const int index) {
                    if (index == 0) cfg.Keyboard.*Field = {};
                    else if (index > 0 && index <= static_cast<int>(Packs->size()))
                        cfg.Keyboard.*Field = Str::WideToUtf8((*Packs)[index - 1].Filename);
                }};
    }

    constexpr SettingsRow PageRows[] = {
        {.Kind = RowKind::Check, .Label = L"Show the current layout on the overlay",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::ShowLayout>(),
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
        {.Kind = RowKind::Combo, .Label = L"Convert words between",
         .Field = PairField<&KeyboardSettings::PairA, 0>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Field = PackField<&KeyboardSettings::PackA, &_packsA>(), .Items = &_packItemsAFn,
         .Beside = true},
        {.Kind = RowKind::Combo, .Label = L"and",
         .Field = PairField<&KeyboardSettings::PairB, 1>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Field = PackField<&KeyboardSettings::PackB, &_packsB>(), .Items = &_packItemsBFn,
         .Beside = true},
        {.Kind = RowKind::Check, .Label = L"Automatically correct on Space",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::AutoCorrect>(), .Enabled = &_canToggleAutoCorrect},
        {.Kind = RowKind::Check, .Label = L"Don't convert in password fields",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::SkipPasswords>()},
        {.Kind = RowKind::Check, .Label = L"Don't convert in fullscreen apps (games)",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::SkipFullscreen>()},
        {.Kind = RowKind::Slider, .Label = L"Threshold (0 = pack)",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::Threshold>(), .Min = 0, .Max = 200},
        {.Kind = RowKind::SoundPicker, .Label = L"Sound on a fix",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FixSound>()},
        {.Kind = RowKind::Slider, .Label = L"Fix sound volume",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FixSoundVolume>()},
        {.Kind = RowKind::Check, .Label = L"Show each fix as a notification",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FixNotification>()},
        {.Kind = RowKind::Check, .Label = L"Log typed words and decisions",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::LogDecisions>()},
        {.Kind = RowKind::Custom, .Label = L"Excluded apps and learned words", .Height = PanelHeight,
         .Create = _createListsPanel},
    };

} // anonymous namespace

namespace KeyboardPage {

    void Register(AppConfig& config) {
        _config = &config;
        SettingsHost::AddPage({.Title = L"Keyboard", .Rows = PageRows});
    }
}
