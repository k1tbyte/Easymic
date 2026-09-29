#include "SettingsPages.hpp"

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include "ActionList.hpp"
#include "SettingsRows.hpp"
#include "Core/Overlay.hpp"
#include "Core/SettingsHost.hpp"
#include "Core/Tray.hpp"
#include "Overlay/TextLayer.hpp"
#include "Resources/Resource.h"
#include "Logger.hpp"
#include "Str.hpp"
#include "UACService.hpp"
#include "Version.hpp"
#include "definitions.h"

namespace {

    AppConfig* _config = nullptr;

    /// Autostart lives in the registry, not the config, so what the box says waits here until OK -
    /// closing with Cancel must leave the machine exactly as it was.
    bool _autoStartWas = false;
    bool _autoStartWants = false;

    constexpr auto AutoStartKey = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)";

    bool _isAutoStart() {
        return RegGetValueW(HKEY_CURRENT_USER, AutoStartKey, APP_NAME, RRF_RT_REG_SZ, nullptr, nullptr, nullptr)
               == ERROR_SUCCESS;
    }

    void _setAutoStart(const bool on) {
        if (!on) {
            RegDeleteKeyValueW(HKEY_CURRENT_USER, AutoStartKey, APP_NAME);
            return;
        }

        // Quoted, or a path with spaces is a command line Windows has to guess at
        wchar_t command[MAX_PATH + 2] = L"\"";
        const DWORD length = GetModuleFileNameW(nullptr, command + 1, MAX_PATH);
        if (length == 0 || length == MAX_PATH) {
            return;
        }
        command[length + 1] = L'"';
        command[length + 2] = L'\0';
        RegSetKeyValueW(HKEY_CURRENT_USER, AutoStartKey, APP_NAME, REG_SZ, command, (length + 3) * sizeof(wchar_t));
    }

    std::vector<std::wstring> _fontNames;
    std::vector<const wchar_t*> _fontItems;
    HFONT _linkFont = nullptr;

    /// Posted to the log box when a line is logged: the line can come from any thread, and the
    /// logger must never end up waiting on the UI thread.
    constexpr UINT WM_LOG_REFRESH = WM_APP + 10;

    /// Offers to restart elevated. @return true when the app is on its way out.
    bool _requestElevation(HWND owner, AppConfig& cfg, const wchar_t* feature) {
        const std::wstring message = std::wstring(L"Administrator privileges are required for ") + feature
            + L".\nWould you like to restart the application as administrator?";

        if (MessageBoxW(owner, message.c_str(), L"Administrator Required", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return false;
        }

        // Restarting elevated is the commit: the elevated instance reads the file, so the choice has
        // to survive the process it was made in. RequestElevation does not return when it succeeds.
        cfg.Save();
        return UAC::RequestElevation();
    }

    /// Declined, or the OS refused - put the field and the file back the way we found them.
    void _elevateOrRevert(HWND owner, AppConfig& cfg, bool& field, const wchar_t* feature) {
        if (!_requestElevation(owner, cfg, feature)) {
            field = !field;
            cfg.Save();
        }
    }

    void _loadFonts() {
        const HDC dc = GetDC(nullptr);
        LOGFONTW query{.lfCharSet = DEFAULT_CHARSET};
        EnumFontFamiliesExW(dc, &query, [](const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM) -> int {
            if (font->lfFaceName[0] != L'@') {
                _fontNames.emplace_back(font->lfFaceName);
            }
            return 1;
        }, 0, 0);
        ReleaseDC(nullptr, dc);

        std::ranges::sort(_fontNames);
        _fontNames.erase(std::ranges::unique(_fontNames).begin(), _fontNames.end());
        std::erase_if(_fontNames, [](const std::wstring& name) {
            return Gdiplus::FontFamily(name.c_str()).GetLastStatus() != Gdiplus::Ok;
        });
        for (const auto& name : _fontNames) {
            _fontItems.push_back(name.c_str());
        }
    }

    int _fontIndex(const std::wstring& name) {
        const auto it = std::ranges::find(_fontNames, name);
        return it == _fontNames.end() ? -1 : static_cast<int>(it - _fontNames.begin());
    }

    /**
     * @brief Reloads the log box from the file on WM_LOG_REFRESH.
     *
     * Wholesale rather than appending the one new line: only errors are logged, so this runs
     * almost never, and the refresh is a bare message with nothing to marshal across threads.
     */
    LRESULT CALLBACK _logProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                              DWORD_PTR subscription) {
        if (message == WM_LOG_REFRESH) {
            SetWindowTextW(edit, Str::Utf8ToWide(Logger::GetLogText()).c_str());
            const int length = GetWindowTextLengthW(edit);
            SendMessageW(edit, EM_SETSEL, length, length);
            SendMessageW(edit, EM_SCROLLCARET, 0, 0);
            return 0;
        }

        if (message == WM_NCDESTROY) {
            Logger::OnLogAdded -= static_cast<int>(subscription);
            RemoveWindowSubclass(edit, _logProc, id);
        }
        return DefSubclassProc(edit, message, wParam, lParam);
    }

    void _createLog(HWND page, const RECT& cell, int) {
        const HWND edit = SettingsRows::Control(page, WC_EDITW, L"",
                                                ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                                                cell, IDC_ABOUT_LOG_LIST, WS_EX_CLIENTEDGE);
        // The default limit of 30,000 characters cuts the newest lines off a longer log
        SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);

        // Only posts: reaching into the control from the logging thread would make the logger wait
        // on the UI thread while it still holds the file - which the UI thread's next log call would
        // then wait on
        const int subscription = Logger::OnLogAdded += [edit](Logger::Level, const std::string&, const std::string&) {
            PostMessageW(edit, WM_LOG_REFRESH, 0, 0);
        };
        SetWindowSubclass(edit, _logProc, 0, subscription);

        // First fill takes the same path as every later line
        PostMessageW(edit, WM_LOG_REFRESH, 0, 0);
    }

    void _createLinks(HWND page, const RECT& cell, int id) {
        SettingsRows::Control(page, WC_STATICW, Str::Utf8ToWide("Version " + g_AppVersion.GetFullFormat()).c_str(),
                              SS_LEFT, {cell.left, cell.top, cell.left + 100, cell.bottom}, -1);
        const HWND link = SettingsRows::Control(page, WC_STATICW, L"GitHub", SS_LEFT | SS_NOTIFY,
                                                {cell.left + 105, cell.top, cell.left + 185, cell.bottom}, id);

        // Underlined copy of the page font, so the link scales with the rest of the page.
        // Once per session: the page is destroyed and rebuilt on every visit.
        if (!_linkFont) {
            LOGFONTW logFont{};
            GetObjectW((HFONT)SendMessageW(page, WM_GETFONT, 0, 0), sizeof(logFont), &logFont);
            logFont.lfUnderline = TRUE;
            _linkFont = CreateFontIndirectW(&logFont);
        }
        SendMessageW(link, WM_SETFONT, (WPARAM)_linkFont, TRUE);
    }

    constexpr SettingsRow General[] = {
        {.Kind = RowKind::Check, .Label = L"Start with Windows",
         .Field = {.Get = [](const AppConfig&) { return int{_autoStartWants}; },
                   .Set = [](AppConfig&, int value) { _autoStartWants = value; }},
         .Commit = [](HWND, AppConfig&, const AppConfig&) {
             if (_autoStartWants != _autoStartWas) {
                 _setAutoStart(_autoStartWants);
             }
         }},
        // Left enabled without elevation on purpose - clicking it is what offers the restart
        {.Kind = RowKind::Check, .Label = L"Skip UAC (Administrator required)",
         .Field = Bind<&AppConfig::Core, &CoreSettings::SkipUac>(),
         .Changed = [](HWND owner, AppConfig& cfg) {
             if (!UAC::IsElevated()) {
                 _elevateOrRevert(owner, cfg, cfg.Core.SkipUac, L"UAC bypass");
             }
         },
         .Commit = [](HWND owner, AppConfig& cfg, const AppConfig& before) {
             if (cfg.Core.SkipUac == before.Core.SkipUac
                 || (cfg.Core.SkipUac ? UAC::EnableSkipUAC() : UAC::DisableSkipUAC())) {
                 return;
             }
             MessageBoxW(owner, cfg.Core.SkipUac ? L"Failed to create UAC bypass task."
                                                 : L"Failed to remove UAC bypass task.",
                         L"Error", MB_OK | MB_ICONERROR);
             cfg.Core.SkipUac = before.Core.SkipUac;
         }},
        {.Kind = RowKind::Check, .Label = L"Check for updates",
         .Field = Bind<&AppConfig::Core, &CoreSettings::Updates>(),
         // Auto-update cannot outlive the check that feeds it
         .Changed = [](HWND, AppConfig& cfg) { cfg.Core.AutoUpdate = cfg.Core.AutoUpdate && cfg.Core.Updates; }},
        {.Kind = RowKind::Check, .Label = L"Enable auto-updates",
         .Field = Bind<&AppConfig::Core, &CoreSettings::AutoUpdate>(),
         .Enabled = [](const AppConfig& cfg) { return cfg.Core.Updates; }},
    };

    constexpr SettingsRow OverlayRows[] = {
        {.Kind = RowKind::Check, .Label = L"Exclude from capture",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::ExcludeFromCapture>()},
        {.Kind = RowKind::Check, .Label = L"On top of all the windows (experimental)",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::OnTopExclusive>(),
         .Changed = [](HWND owner, AppConfig& cfg) {
             if (cfg.Overlay.OnTopExclusive && !UAC::IsElevated()) {
                 _elevateOrRevert(owner, cfg, cfg.Overlay.OnTopExclusive, L"the 'On top of all windows' feature");
             }
         }},
        {.Kind = RowKind::Check, .Label = L"Show action notifications",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::Notifications>()},
        {.Kind = RowKind::Slider, .Label = L"Size (px)",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::Size>(), .Min = 10, .Max = 32,
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
        {.Kind = RowKind::Slider, .Label = L"Opacity (%)",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::Opacity>(), .Min = 20, .Max = 100, .Step = 10,
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
        {.Kind = RowKind::Combo, .Label = L"Text font",
         .Field = {.Get = [](const AppConfig& cfg) {
                       const int selected = _fontIndex(Str::Utf8ToWide(cfg.Overlay.FontFamily));
                       return selected >= 0 ? selected : _fontIndex(L"Segoe UI");
                   },
                   .Set = [](AppConfig& cfg, int index) {
                       if (index >= 0 && index < static_cast<int>(_fontNames.size())) {
                           cfg.Overlay.FontFamily = Str::WideToUtf8(_fontNames[index]);
                       }
                   }},
         .Items = [] {
             // Once per session: every slider tick syncs the page, and the list takes a font enumeration
             if (_fontItems.empty()) {
                 _loadFonts();
             }
             return std::span<const wchar_t* const>{_fontItems};
         },
         // A sample in the text pill, which the overlay shows while settings are open.
         // MSVC's source codepage cannot reliably decode Cyrillic literals.
         .Changed = [](HWND, AppConfig&) {
             TextLayer::Text = {L'A', L'a', L' ', 0x042F, 0x044F};
             Overlay::Changed();
         }},
    };

    constexpr SettingsRow TrayRows[] = {
        {.Kind = RowKind::Radio, .Label = L"Icon shows",
         .Field = {.Get = [](const AppConfig& cfg) {
                       const auto choices = Tray::Choices();
                       return static_cast<int>(std::ranges::find(choices, Tray::Owner(cfg.Tray.Provider)) - choices.begin());
                   },
                   .Set = [](AppConfig& cfg, int index) { cfg.Tray.Provider = Tray::Choices()[index]->Id; }},
         .Items = [] {
             static std::vector<const wchar_t*> titles;
             titles.clear();
             for (const TrayProvider* provider : Tray::Choices()) {
                 titles.push_back(provider->Title);
             }
             return std::span<const wchar_t* const>{titles};
         },
         // The icon follows the click; Cancel puts it back when the frame restores the config
         .Changed = [](HWND, AppConfig&) { Tray::Changed(); }},
    };

    constexpr SettingsRow Hotkeys[] = {
        {.Kind = RowKind::Text, .Label = L"Double-click an action to edit it:"},
        {.Kind = RowKind::Custom, .Height = 122,
         .Create = [](HWND page, const RECT& cell, int) { ActionList::Create(page, cell, *_config); }},
        {.Kind = RowKind::Slider, .Label = L"Multi-press window (ms)",
         .Field = Bind<&AppConfig::Core, &CoreSettings::MultiPressWindowMs>(), .Min = 100, .Max = 600, .Step = 10},
    };

    constexpr SettingsRow About[] = {
        {.Kind = RowKind::Custom,
         .Changed = [](HWND, AppConfig&) {
             ShellExecuteW(nullptr, L"open", Str::Utf8ToWide(REPO_URL).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
         },
         .Height = 10, .Create = _createLinks},
        {.Kind = RowKind::Custom, .Height = 150, .Create = _createLog},
    };

} // anonymous namespace

void SettingsPages::Register(AppConfig& config) {
    _config = &config;
    SettingsHost::AddPage({.Title = L"General", .Rows = General, .Order = SettingsHost::First});
    SettingsHost::AddPage({.Title = L"Overlay", .Rows = OverlayRows});
    SettingsHost::AddPage({.Title = L"Tray", .Rows = TrayRows});
    SettingsHost::AddPage({.Title = L"Hotkeys", .Rows = Hotkeys});
    SettingsHost::AddPage({.Title = L"About", .Rows = About, .Order = SettingsHost::Last});
}

void SettingsPages::Open() {
    _autoStartWas = _isAutoStart();
    _autoStartWants = _autoStartWas;
}

void SettingsPages::Close() {
    _fontNames.clear();
    _fontItems.clear();
    if (_linkFont) {
        DeleteObject(_linkFont);
        _linkFont = nullptr;
    }
}
