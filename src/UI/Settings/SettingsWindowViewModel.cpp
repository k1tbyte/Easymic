#include "SettingsWindowViewModel.hpp"
#include "SettingsRows.hpp"
#include "Core/Hotkeys/KeyNames.hpp"
#include "Core/Overlay.hpp"
#include "MainWindow.hpp"
#include "Resources/Resource.h"
#include "definitions.h"
#include "Registry.hpp"
#include "Str.hpp"
#include "Version.hpp"
#include "UACService.hpp"
#include <windows.h>
#include <commctrl.h>

namespace {
    /// At most one settings window is ever open - MainWindowViewModel::OpenSettings brings the
    /// existing one forward instead of making a second - so a row's hook can be the plain
    /// function pointer the registry wants and still find the live view model.
    SettingsWindowViewModel* _current = nullptr;

    /// Autostart lives in the registry, not the config, so what the box says waits here until OK -
    /// closing with Cancel must leave the machine exactly as it was.
    bool _autoStartWas = false;
    bool _autoStartWants = false;

    /// Offers to restart elevated. @return true when the app is on its way out.
    bool RequestElevationFor(HWND owner, AppConfig& cfg, const wchar_t* feature) {
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
    void ElevateOrRevert(HWND owner, AppConfig& cfg, bool& field, const wchar_t* feature) {
        if (!RequestElevationFor(owner, cfg, feature)) {
            field = !field;
            cfg.Save();
        }
    }
}

SettingsWindowViewModel::~SettingsWindowViewModel() {
    CleanupLogDisplay();
    if (_linkFont) {
        DeleteObject(_linkFont);
    }
    if (_current == this) {
        _current = nullptr;
    }
}

void SettingsWindowViewModel::RegisterPages() {
    static constexpr SettingsRow General[] = {
        {.Kind = RowKind::Check, .Label = L"Start with Windows",
         .Field = {.Get = [](const AppConfig&) { return int{_autoStartWants}; },
                   .Set = [](AppConfig&, int value) { _autoStartWants = value; }},
         .Commit = [](HWND, AppConfig&, const AppConfig&) {
             if (_autoStartWants != _autoStartWas) {
                 _autoStartWants ? Registry::AddToAutoStartup(APP_NAME) : Registry::RemoveFromAutoStartup(APP_NAME);
             }
         }},
        // Left enabled without elevation on purpose - clicking it is what offers the restart
        {.Kind = RowKind::Check, .Label = L"Skip UAC (Administrator required)",
         .Field = Bind<&AppConfig::Core, &CoreSettings::SkipUac>(),
         .Changed = [](HWND owner, AppConfig& cfg) {
             if (!UAC::IsElevated()) {
                 ElevateOrRevert(owner, cfg, cfg.Core.SkipUac, L"UAC bypass");
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

    static constexpr SettingsRow OverlayRows[] = {
        {.Kind = RowKind::Check, .Label = L"Exclude from capture",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::ExcludeFromCapture>()},
        {.Kind = RowKind::Check, .Label = L"On top of all the windows (experimental)",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::OnTopExclusive>(),
         .Changed = [](HWND owner, AppConfig& cfg) {
             if (cfg.Overlay.OnTopExclusive && !UAC::IsElevated()) {
                 ElevateOrRevert(owner, cfg, cfg.Overlay.OnTopExclusive, L"the 'On top of all windows' feature");
             }
         }},
        {.Kind = RowKind::Check, .Label = L"Show action notifications",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::Notifications>()},
        {.Kind = RowKind::Slider, .Label = L"Size (px)",
         .Field = Bind<&AppConfig::Overlay, &OverlaySettings::Size>(), .Min = 10, .Max = 32,
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
    };

    static constexpr SettingsRow Hotkeys[] = {
        {.Kind = RowKind::Text, .Label = L"Double-click an action to edit it:"},
        {.Kind = RowKind::Custom, .Height = 122,
         .Create = [](HWND page, const RECT& cell, int) {
             SettingsRows::Control(page, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                                   cell, IDC_HOTKEYS_LIST, WS_EX_CLIENTEDGE);
             _current->RefreshActionRows();
         }},
        {.Kind = RowKind::Slider, .Label = L"Multi-press window (ms)",
         .Field = Bind<&AppConfig::Core, &CoreSettings::MultiPressWindowMs>(), .Min = 100, .Max = 600, .Step = 10},
    };

    static constexpr SettingsRow About[] = {
        {.Kind = RowKind::Custom,
         .Changed = [](HWND, AppConfig&) {
             ShellExecuteW(nullptr, L"open", Str::Utf8ToWide(REPO_URL).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
         },
         .Height = 10,
         .Create = [](HWND page, const RECT& cell, int id) {
             SettingsRows::Control(page, WC_STATICW, Str::Utf8ToWide("Version " + g_AppVersion.GetFullFormat()).c_str(),
                                   SS_LEFT, {cell.left, cell.top, cell.left + 100, cell.bottom}, -1);
             HWND link = SettingsRows::Control(page, WC_STATICW, L"GitHub", SS_LEFT | SS_NOTIFY,
                                               {cell.left + 105, cell.top, cell.left + 185, cell.bottom}, id);

             // Underlined copy of the page font, so the link scales with the rest of the page.
             // Created once: the page is destroyed and rebuilt on every visit.
             if (!_current->_linkFont) {
                 LOGFONTW logFont{};
                 GetObjectW((HFONT)SendMessageW(page, WM_GETFONT, 0, 0), sizeof(logFont), &logFont);
                 logFont.lfUnderline = TRUE;
                 _current->_linkFont = CreateFontIndirectW(&logFont);
             }
             SendMessageW(link, WM_SETFONT, (WPARAM)_current->_linkFont, TRUE);
         }},
        {.Kind = RowKind::Custom, .Height = 150,
         .Create = [](HWND page, const RECT& cell, int) {
             SettingsRows::Control(page, WC_EDITW, L"",
                                   ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                                   cell, IDC_ABOUT_LOG_LIST, WS_EX_CLIENTEDGE);
             _current->SetupLogDisplay(page);
         }},
    };

    SettingsHost::AddPage({.Title = L"General", .Rows = General, .Order = SettingsHost::First});
    SettingsHost::AddPage({.Title = L"Overlay", .Rows = OverlayRows});
    SettingsHost::AddPage({.Title = L"Hotkeys", .Rows = Hotkeys});
    SettingsHost::AddPage({.Title = L"About", .Rows = About, .Order = SettingsHost::Last});
}

/// One row per binding in config order, then the row that adds another.
void SettingsWindowViewModel::RefreshActionRows() const {
    // The trigger already stores the display name, so there is nothing to format here
    const auto describe = [](const HotkeyTrigger& trigger) {
        std::string hotkey = trigger.Keys;
        if (hotkey.empty()) {
            return hotkey;
        }

        if (trigger.Presses > 1) {
            hotkey += " x" + std::to_string(trigger.Presses);
        }
        if (trigger.OnRelease) {
            hotkey += " (release)";
        }
        return hotkey;
    };

    std::vector<ActionRow> rows;
    rows.reserve(_cfg.Bindings.size() + 1);

    for (const auto& binding : _cfg.Bindings) {
        const ActionDesc* const desc = ActionRegistry::Find(binding.ActionId);
        rows.push_back({
            .Name = binding.Name,
            .Hotkey = describe(binding.Trigger),
            // The last column is the action's own argument, whatever that action reads it as
            .Command = binding.Args,
            .RunsCommand = desc && HasFlag(desc->Flags, ActionFlags::RunsCommand)
        });
    }

    rows.push_back({.Name = "+ Add action..."});
    _view->SetActionRows(rows);
}

/**
 * @brief Frees a combination from every other binding.
 *
 * What has to be unique is the pair: the same combination may drive several bindings as long as
 * each wants a different number of presses. Comparing the names rather than the masks is safe
 * because every stored name came out of KeyNames::Format, which is canonical.
 */
void SettingsWindowViewModel::ClearHotkey(const std::string& keys, const uint8_t presses,
                                          const int exceptIndex) {
    if (keys.empty()) {
        return;
    }

    for (int i = 0; i < static_cast<int>(_cfg.Bindings.size()); i++) {
        if (i != exceptIndex && _cfg.Bindings[i].Trigger.Keys == keys
            && _cfg.Bindings[i].Trigger.Presses == presses) {
            _cfg.Bindings[i].Trigger.Keys.clear();
        }
    }
}

void SettingsWindowViewModel::HandleActionActivated(int rowIndex) {
    if (rowIndex < static_cast<int>(_cfg.Bindings.size())) {
        EditAction(rowIndex, {});
    } else {
        AddAction();
    }

    RefreshActionRows();
}

/**
 * @brief Asks which action the new entry runs, then opens it for editing.
 *
 * An action is an entry like any other, so the list can hold several of the same one - which is
 * the point of picking it here rather than having one fixed row per action.
 */
void SettingsWindowViewModel::AddAction() {
    HMENU menu = CreatePopupMenu();

    const int count = static_cast<int>(ActionRegistry::All.size());
    std::string_view group;
    for (int i = 0; i < count; i++) {
        const ActionDesc& desc = ActionRegistry::All[i];
        // Registration order is the module order in main, so one group's actions are contiguous.
        // Anything that breaks that has to sort the menu rather than separate it.
        if (i && desc.Group != group) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        group = desc.Group;
        AppendMenuW(menu, MF_STRING, i + 1, Str::Utf8ToWide(std::string{desc.Title}).c_str());
    }

    POINT cursor;
    GetCursorPos(&cursor);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0,
                                      _view->GetHandle(), nullptr);
    DestroyMenu(menu);

    if (!chosen) {
        return;
    }

    const ActionDesc& desc = ActionRegistry::All[chosen - 1];
    EditAction(static_cast<int>(_cfg.Bindings.size()),
               {.Name = std::string{desc.Title},
                .ActionId = std::string{desc.Id},
                .Sound = std::string{desc.DefaultSound}});
}

/// An index past the end is a new binding, and then the seed says which action it runs.
void SettingsWindowViewModel::EditAction(int index, const Binding& seed) {
    const bool isExisting = index < static_cast<int>(_cfg.Bindings.size());
    const Binding stored = isExisting ? _cfg.Bindings[index] : seed;
    const ActionDesc* const desc = ActionRegistry::Find(stored.ActionId);
    const bool runsCommand = desc && HasFlag(desc->Flags, ActionFlags::RunsCommand);

    ActionEdit edit{
        .Title = stored.Name.empty() ? "New action" : stored.Name,
        .Name = stored.Name,
        // One stored field behind two rows - which of them the dialog shows is the action's own
        // business, and no action has both
        .Command = runsCommand ? stored.Args : "",
        .Args = runsCommand ? "" : stored.Args,
        // An id no module registered still shows its argument: the binding cannot fire, and a row
        // the user cannot even read is a worse way to say so than a labelled one they can fix
        .ArgsLabel = desc ? (runsCommand ? "" : std::string{desc->ArgsLabel})
                          : (stored.Args.empty() ? "" : "Argument"),
        .ArgsHint = desc ? std::string{desc->ArgsHint} : "",
        .Sound = stored.Sound,
        .SoundVolume = stored.SoundVolume,
        // The box always shows what will actually appear on screen, template included
        .Notification = !stored.Notification.empty() ? stored.Notification
                        : desc ? std::string{desc->DefaultNotification}
                               : std::string{ActionRegistry::DefaultNotification},
        // The dialog and the capture both work in masks; the name is what goes to disk
        .Hotkey = KeyNames::Parse(stored.Trigger.Keys),
        .OnRelease = stored.Trigger.OnRelease,
        .Presses = stored.Trigger.Presses,
        .Block = stored.Trigger.Block,
        .TapOnly = stored.Trigger.TapOnly,
        .ShowNotification = stored.ShowNotification,
        .RunsCommand = runsCommand,
        .HasSound = !desc || !HasFlag(desc->Flags, ActionFlags::NoSound),
        // Carrying a release factory is what makes "trigger on release" meaningless for an action
        .HoldOnly = desc && desc->MakeRelease != nullptr,
        .AllowDelete = isExisting,
    };

    if (!_view->ShowActionDialog(edit, _cfg.RecentSounds)) {
        return;
    }

    if (edit.Deleted) {
        _cfg.Bindings.erase(_cfg.Bindings.begin() + index);
        return;
    }

    const std::string keys = edit.Hotkey ? KeyNames::Format(edit.Hotkey) : std::string{};
    ClearHotkey(keys, edit.Presses, isExisting ? index : -1);

    // Starts from what was stored so the action id the entry points at survives the edit
    Binding binding = stored;
    binding.Name = edit.Name;
    binding.Args = runsCommand ? edit.Command : edit.Args;
    binding.Sound = edit.Sound;
    binding.Notification = edit.Notification;
    binding.SoundVolume = edit.SoundVolume;
    binding.ShowNotification = edit.ShowNotification;
    binding.Trigger = {.Keys = keys,
                       .Presses = edit.Presses,
                       .OnRelease = edit.OnRelease,
                       .Block = edit.Block,
                       .TapOnly = edit.TapOnly};

    if (isExisting) {
        _cfg.Bindings[index] = binding;
    } else {
        _cfg.Bindings.push_back(binding);
    }
}

void SettingsWindowViewModel::SetupLogDisplay(HWND hWnd) {
    CleanupLogDisplay();

    // Captures the page by value and only posts: a log line can arrive on any thread, and
    // reaching into a control from there would make the logger wait on the UI thread while it
    // still holds the file - which the UI thread's own next log call would then wait on.
    _logAddedSubscriptionId = Logger::OnLogAdded += [page = hWnd](Logger::Level, const std::string&,
                                                                  const std::string&) {
        PostMessageW(page, SettingsWindow::WM_LOG_REFRESH, 0, 0);
    };

    // First fill takes the same path as every later line
    PostMessageW(hWnd, SettingsWindow::WM_LOG_REFRESH, 0, 0);
}

void SettingsWindowViewModel::CleanupLogDisplay() {
    if (_logAddedSubscriptionId != -1) {
        Logger::OnLogAdded -= _logAddedSubscriptionId;
        _logAddedSubscriptionId = -1;
    }
}

void SettingsWindowViewModel::Init() {
    _current = this;
    _cfgPrev = _cfg;
    _autoStartWas = Registry::IsInAutoStartup(APP_NAME);
    _autoStartWants = _autoStartWas;

    _mainWindow = static_cast<MainWindow*>(_view->GetParent());
    // Not shown from here: SuspendActivity already put the overlay into preview, and whether it
    // is on screen is the surface's decision alone
    _mainWindow->ToggleInteractivity(true);

    _view->OnPageCreated = [this](HWND page, const SettingsPage& desc) {
        // The page the subscription posts to was just destroyed; the About page makes a new one
        CleanupLogDisplay();
        _rows = desc.Rows;
        SettingsRows::Build(page, _rows, _cfg);
    };
    _view->OnPageInput = [this](HWND page, UINT message, WPARAM wParam, LPARAM lParam) {
        SettingsRows::Handle(page, _rows, _cfg, message, wParam, lParam);
    };
    _view->OnActionActivated = [this](int rowIndex) { HandleActionActivated(rowIndex); };

    _view->OnApply += [this] {
        _mainWindow->UpdateRect();
        _cfg.Overlay.PosX = _mainWindow->GetPositionX();
        _cfg.Overlay.PosY = _mainWindow->GetPositionY();

        // Every page's rows, not only the open one's - a choice made on a page left earlier is
        // still waiting for this
        for (const SettingsPage& page : SettingsHost::Pages) {
            for (const SettingsRow& row : page.Rows) {
                if (row.Commit) {
                    row.Commit(_view->GetHandle(), _cfg, _cfgPrev);
                }
            }
        }

        if (_cfg != _cfgPrev) {
            _cfg.Save();
            _cfgPrev = _cfg;
        }
    };

    _view->OnExit += [this] {
        // Raised in the frame's WM_DESTROY, while the page it posts to still exists
        CleanupLogDisplay();
        _mainWindow->ToggleInteractivity(false);
        _cfg = _cfgPrev;
    };
}
