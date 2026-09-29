#include "Page.hpp"

#include "AppConfig.hpp"
#include "Controls.hpp"
#include "Core/SettingsHost.hpp"
#include "Editor/Editor.hpp"
#include "Placer.hpp"
#include "Preview.hpp"
#include "Str.hpp"
#include "VirtualDesktops.hpp"
#include "WindowCatalog.hpp"
#include "WindowList.hpp"

#include <algorithm>
#include <commctrl.h>

namespace {

    enum : int { IdTabs = 100, IdName, IdDelete, IdPreview, IdList, IdAdd, IdRemove, IdCapture, IdArrange, IdCustomize };

    constexpr wchar_t PanelClass[] = L"EasyLauncher.DesktopsPanel";
    constexpr int PanelHeight = 134; // dialog units

    AppConfig* _config = nullptr;
    HWND _panel = nullptr;
    int _selected = 0;
    /// What the shell calls each real desktop, read once per visit.
    std::vector<std::wstring> _shellNames;
    /// Set while the page writes the name box itself, so its EN_CHANGE is not taken for typing.
    bool _filling = false;

    std::vector<DesktopPreset>& _presets() {
        return _config->Desktops.Presets;
    }

    const DesktopPreset* _existing() {
        return static_cast<size_t>(_selected) < _presets().size() ? &_presets()[_selected] : nullptr;
    }

    /// The selected desktop's preset, made on the first write to it.
    DesktopPreset& _preset() {
        if (!_existing()) {
            _presets().resize(_selected + 1);
        }
        return _presets()[_selected];
    }

    int _desktopCount() {
        return std::max(static_cast<int>(_shellNames.size()), static_cast<int>(_presets().size()));
    }

    std::wstring _shellName(const int index) {
        return index < static_cast<int>(_shellNames.size()) ? _shellNames[index]
                                                           : L"Desktop " + std::to_wstring(index + 1);
    }

    std::wstring _label(const int index) {
        const bool named = index < static_cast<int>(_presets().size()) && !_presets()[index].Name.empty();
        return named ? Str::Utf8ToWide(_presets()[index].Name) : _shellName(index);
    }

    HWND _item(const int id) {
        return GetDlgItem(_panel, id);
    }

    void _setTab(const int index, std::wstring text, const bool insert) {
        TCITEMW item{.mask = TCIF_TEXT, .pszText = text.data()};
        SendMessageW(_item(IdTabs), insert ? TCM_INSERTITEMW : TCM_SETITEMW, index, reinterpret_cast<LPARAM>(&item));
    }

    void _fillTabs() {
        TabCtrl_DeleteAllItems(_item(IdTabs));
        const int count = _desktopCount();
        for (int i = 0; i < count; ++i) {
            _setTab(i, _label(i), true);
        }
        _setTab(count, L"+", true);
        TabCtrl_SetCurSel(_item(IdTabs), _selected);
    }

    void _fillList() {
        HWND list = _item(IdList);
        ListView_DeleteAllItems(list);
        static const std::vector<WindowRule> none;
        const DesktopPreset* preset = _existing();
        const std::vector<WindowRule>& windows = preset ? preset->Windows : none;

        for (int i = 0; i < static_cast<int>(windows.size()); ++i) {
            const WindowRule& rule = windows[i];
            std::wstring exe = Str::Utf8ToWide(rule.Exe);
            LVITEMW item{.mask = LVIF_TEXT, .iItem = i, .pszText = exe.data()};
            ListView_InsertItem(list, &item);

            wchar_t place[48];
            if (rule.Width <= 0 || rule.Height <= 0) {
                wcscpy_s(place, L"Anywhere");
            } else if (rule.Maximized) {
                wcscpy_s(place, L"Maximized");
            } else {
                swprintf_s(place, L"%d, %d   %d x %d", rule.X, rule.Y, rule.Width, rule.Height);
            }
            ListView_SetItemText(list, i, 1, place);
        }
        Controls::FitColumns(list, {45});
    }

    void _fillDesktop() {
        const DesktopPreset* preset = _existing();
        _filling = true;
        SetWindowTextW(_item(IdName), preset ? Str::Utf8ToWide(preset->Name).c_str() : L"");
        _filling = false;
        // The shell's name shows through an empty box, which is what leaving it alone looks like
        SendMessageW(_item(IdName), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(_shellName(_selected).c_str()));

        EnableWindow(_item(IdDelete), preset != nullptr);
        EnableWindow(_item(IdCapture), _selected < static_cast<int>(_shellNames.size()));
        _fillList();
        InvalidateRect(_item(IdPreview), nullptr, FALSE);
    }

    void _select(const int index) {
        const bool added = index == _desktopCount();
        if (added) {
            _presets().resize(index + 1);
        }
        _selected = index;
        if (added) {
            _fillTabs();
        }
        _fillDesktop();
    }

    std::vector<HWND> _foreignWindows() {
        std::vector<HWND> windows;
        for (const HWND window : WindowCatalog::AppWindows()) {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process != GetCurrentProcessId()) {
                windows.push_back(window);
            }
        }
        return windows;
    }

    void _addWindow() {
        const std::vector<HWND> windows = _foreignWindows();
        const HMENU menu = CreatePopupMenu();
        for (size_t i = 0; i < windows.size(); ++i) {
            wchar_t title[80]{};
            GetWindowTextW(windows[i], title, static_cast<int>(std::size(title)));
            AppendMenuW(menu, MF_STRING, i + 1, (WindowCatalog::ExeName(windows[i]) + L"  -  " + title).c_str());
        }

        RECT button;
        GetWindowRect(_item(IdAdd), &button);
        const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, button.left, button.bottom, 0,
                                          _panel, nullptr);
        DestroyMenu(menu);
        if (chosen > 0) {
            _preset().Windows.push_back(WindowList::Capture(windows[chosen - 1]));
            _fillDesktop();
        }
    }

    void _removeWindow() {
        const int row = ListView_GetNextItem(_item(IdList), -1, LVNI_SELECTED);
        if (const DesktopPreset* preset = _existing(); preset && row >= 0 && row < static_cast<int>(preset->Windows.size())) {
            _preset().Windows.erase(_preset().Windows.begin() + row);
            _fillDesktop();
        }
    }

    /// Asks the shell per window - fine here: the hooks are down while settings are open.
    void _capture() {
        std::vector<WindowRule> captured;
        for (const HWND window : _foreignWindows()) {
            if (VirtualDesktops::DesktopOf(window) == _selected) {
                captured.push_back(WindowList::Capture(window));
            }
        }
        _preset().Windows = std::move(captured);
        _fillDesktop();
    }

    void _delete() {
        if (!_existing()) {
            return;
        }
        // Presets are bound by position: clear this one, drop only the empty tail
        _presets()[_selected] = {};
        while (!_presets().empty() && _presets().back() == DesktopPreset{}) {
            _presets().pop_back();
        }
        _selected = std::max(0, std::min(_selected, _desktopCount() - 1));
        _fillTabs();
        _fillDesktop();
    }

    void _command(const int id, const int code) {
        if (id == IdName) {
            if (code == EN_CHANGE && !_filling) {
                wchar_t name[256];
                GetWindowTextW(_item(IdName), name, static_cast<int>(std::size(name)));
                _preset().Name = Str::WideToUtf8(name);
                _setTab(_selected, _label(_selected), false);
                EnableWindow(_item(IdDelete), TRUE);
            }
            return;
        }
        if (code != BN_CLICKED) {
            return;
        }

        switch (id) {
            case IdDelete:  _delete(); break;
            case IdAdd:     _addWindow(); break;
            case IdRemove:  _removeWindow(); break;
            case IdCapture: _capture(); break;
            case IdCustomize:
                if (_existing()) {
                    Editor::Run(_preset());
                    _fillDesktop();
                }
                break;
            case IdArrange:
                Placer::Load(_config->Desktops);
                Placer::PlaceAll();
                break;
            default: break;
        }
    }

    LRESULT CALLBACK _proc(const HWND hwnd, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        switch (message) {
            case WM_COMMAND:
                _command(LOWORD(wParam), HIWORD(wParam));
                return 0;

            case WM_NOTIFY:
                if (const auto* header = reinterpret_cast<const NMHDR*>(lParam);
                    header->idFrom == IdTabs && header->code == TCN_SELCHANGE) {
                    _select(TabCtrl_GetCurSel(header->hwndFrom));
                }
                return 0;

            case WM_DRAWITEM: {
                static const std::vector<WindowRule> none;
                const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                const DesktopPreset* preset = _existing();
                Preview::Paint(draw->hDC, draw->rcItem, preset ? preset->Windows : none,
                               9.f * GetDpiForWindow(hwnd) / 96);
                return TRUE;
            }

            case WM_DESTROY:
                _panel = nullptr;
                break;

            default:
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void _create(const HWND page, const RECT& cell, const int id) {
        static const bool registered = [] {
            const WNDCLASSW windowClass{.lpfnWndProc = _proc,
                                        .hInstance = GetModuleHandleW(nullptr),
                                        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
                                        .hbrBackground = GetSysColorBrush(COLOR_BTNFACE),
                                        .lpszClassName = PanelClass};
            return RegisterClassW(&windowClass) != 0;
        }();
        if (!registered) {
            return;
        }

        // CONTROLPARENT: the dialog manager has to walk into the panel, or tabbing stops at it
        _panel = Controls::Create(page, page, PanelClass, L"", WS_CLIPCHILDREN, cell, id, WS_EX_CONTROLPARENT);
        const int width = cell.right - cell.left;
        const auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style, const RECT& at,
                             const int childId, const DWORD exStyle = 0) {
            return Controls::Create(page, _panel, cls, text, style, at, childId, exStyle);
        };

        add(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, {0, 0, width, 14}, IdTabs);
        add(WC_STATICW, L"Name", SS_LEFT, {0, 21, 28, 29}, -1);
        add(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, {30, 19, width - 50, 31}, IdName, WS_EX_CLIENTEDGE);
        add(WC_BUTTONW, L"Delete", BS_PUSHBUTTON | WS_TABSTOP, {width - 46, 18, width, 32}, IdDelete);
        add(WC_STATICW, L"", SS_OWNERDRAW, {0, 36, 140, 116}, IdPreview);
        const HWND list = add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP,
                              {144, 36, width, 116}, IdList, WS_EX_CLIENTEDGE);
        add(WC_BUTTONW, L"Add window", BS_PUSHBUTTON | WS_TABSTOP, {0, 120, 56, 134}, IdAdd);
        add(WC_BUTTONW, L"Remove", BS_PUSHBUTTON | WS_TABSTOP, {60, 120, 104, 134}, IdRemove);
        add(WC_BUTTONW, L"Capture", BS_PUSHBUTTON | WS_TABSTOP, {108, 120, 152, 134}, IdCapture);
        add(WC_BUTTONW, L"Customize", BS_PUSHBUTTON | WS_TABSTOP, {156, 120, 204, 134}, IdCustomize);
        add(WC_BUTTONW, L"Arrange now", BS_PUSHBUTTON | WS_TABSTOP, {width - 56, 120, width, 134}, IdArrange);

        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
        LVCOLUMNW column{.mask = LVCF_TEXT, .pszText = const_cast<wchar_t*>(L"App")};
        ListView_InsertColumn(list, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"Place");
        ListView_InsertColumn(list, 1, &column);

        _shellNames = VirtualDesktops::Names();
        _selected = std::clamp(_selected, 0, std::max(_desktopCount() - 1, 0));
        _fillTabs();
        _fillDesktop();
    }

    /// On OK only: a rename reaches the shell once the user keeps it, and Cancel still means Cancel.
    void _commit(HWND, AppConfig& cfg, const AppConfig& before) {
        const auto& now = cfg.Desktops.Presets;
        const auto& was = before.Desktops.Presets;
        std::vector<std::wstring> renamed(now.size());
        for (size_t i = 0; i < now.size(); ++i) {
            if (i >= was.size() || now[i].Name != was[i].Name) {
                renamed[i] = Str::Utf8ToWide(now[i].Name);
            }
        }
        VirtualDesktops::Rename(renamed);
    }

    constexpr SettingsRow PageRows[] = {
        {.Kind = RowKind::Custom, .Commit = _commit, .Height = PanelHeight, .Create = _create},
        {.Kind = RowKind::Check, .Label = L"Place windows as they open",
         .Field = Bind<&AppConfig::Desktops, &DesktopSettings::Watch>()},
        {.Kind = RowKind::Check, .Label = L"Follow a window I just opened to its desktop",
         .Field = Bind<&AppConfig::Desktops, &DesktopSettings::Follow>(),
         .Enabled = [](const AppConfig& cfg) { return cfg.Desktops.Watch; }},
        {.Kind = RowKind::Check, .Label = L"Arrange windows at startup",
         .Field = Bind<&AppConfig::Desktops, &DesktopSettings::ApplyOnStartup>()},
        {.Kind = RowKind::Check, .Label = L"Show the desktop's name when it changes",
         .Field = Bind<&AppConfig::Desktops, &DesktopSettings::AnnounceSwitch>()},
    };

    constexpr SettingsRow UnsupportedRows[] = {
        {.Kind = RowKind::Text, .Label = L"Virtual desktops need Windows 11 24H2 or 25H2."},
    };

} // anonymous namespace

namespace Page {

    void Register(AppConfig& config) {
        _config = &config;
        SettingsHost::AddPage({.Title = L"Desktops",
                               .Rows = VirtualDesktops::Supported() ? std::span<const SettingsRow>(PageRows)
                                                                    : std::span<const SettingsRow>(UnsupportedRows)});
    }
}
