#include "SettingsWindowViewModel.hpp"
#include "HotkeyCapture.hpp"
#include "Core/HotkeyService.hpp"
#include "Core/KeyNames.hpp"
#include "MainWindow.hpp"
#include "Resources/Resource.h"
#include "definitions.h"
#include "Version.hpp"
#include "UACService.hpp"
#include <windows.h>
#include <commctrl.h>

namespace {
    /// At most one settings window is ever open - MainWindowViewModel::OpenSettings brings the
    /// existing one forward instead of making a second - so a page builder can be the plain
    /// function pointer the registry wants and still find the live view model.
    SettingsWindowViewModel* _current = nullptr;
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
    SettingsHost::AddPage({.Title = L"General",
                           .TemplateId = IDD_SETTINGS_GENERAL,
                           .Build = [](HWND page) { _current->InitializeGeneralSection(page); },
                           .Order = SettingsHost::First});

    SettingsHost::AddPage({.Title = L"Indicator",
                           .TemplateId = IDD_SETTINGS_INDICATOR,
                           .Build = [](HWND page) { _current->InitializeIndicatorSection(page); }});

    SettingsHost::AddPage({.Title = L"Sounds",
                           .TemplateId = IDD_SETTINGS_SOUNDS,
                           .Build = [](HWND page) { _current->InitializeSoundsSection(page); }});

    SettingsHost::AddPage({.Title = L"Hotkeys",
                           .TemplateId = IDD_SETTINGS_HOTKEYS,
                           .Build = [](HWND page) { _current->InitializeHotkeysSection(page); }});

    SettingsHost::AddPage({.Title = L"About",
                           .TemplateId = IDD_SETTINGS_ABOUT,
                           .Build = [](HWND page) { _current->InitializeAboutSection(page); },
                           .Order = SettingsHost::Last});
}

void SettingsWindowViewModel::InitializeHotkeysSection(HWND hWnd) const {
    RefreshActionRows();
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_HOTKEYS_WINDOW_TRACKBAR),
                                 10, MAKELONG(100, 600), _cfg.Core.MultiPressWindowMs);
}

void SettingsWindowViewModel::InitializeGeneralSection(HWND hWnd) const {
    // Rendered from the pending state, not from the registry: leaving the page and coming back
    // must not throw away a toggle that has not been applied yet
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTOSTART), BM_SETCHECK, _autoStartRequested, 0);

    // Left enabled without elevation on purpose - clicking it is what offers the restart
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_SKIP_UAC), BM_SETCHECK, _cfg.Core.SkipUac, 0);

    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_UPDATES_ENABLED), BM_SETCHECK,
                _cfg.Core.Updates, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), BM_SETCHECK,
                _cfg.Core.AutoUpdate, 0);
    EnableWindow(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), _cfg.Core.Updates);
}

void SettingsWindowViewModel::InitializeIndicatorSection(HWND hWnd) const {
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_CAPTURE), BM_SETCHECK,
                _cfg.Indicator.ExcludeFromCapture, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_ON_TOP), BM_SETCHECK,
                _cfg.Indicator.OnTopExclusive, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_HIDE_INACTIVE), BM_SETCHECK,
                _cfg.Indicator.HideWhenInactive, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_NOTIFICATIONS), BM_SETCHECK,
                _cfg.Core.Notifications, 0);

    // Setup indicator state combo box
    HWND hCombo = GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_COMBO);
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    for (const auto& state : IndicatorStates) {
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)state);
    }
    SendMessage(hCombo, CB_SETCURSEL, (WPARAM)_cfg.Indicator.State, 0);

    // Setup trackbars
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR),
                       1, MAKELONG(10, 32), _cfg.Indicator.Size);
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR),
                       1, MAKELONG(0, 100), static_cast<int>(_cfg.Indicator.VolumeThreshold * 100));
}

void SettingsWindowViewModel::InitializeSoundsSection(HWND hWnd) const {
    // -1 means "never set", so the slider opens on whatever the device is actually at
    const int micVolume = _cfg.Mic.Volume == -1 ? _audioManager.CaptureDevice()->GetVolumePercent()
                                               : _cfg.Mic.Volume;
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), micVolume);

    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), _cfg.Mic.BellVolume);

    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME), BM_SETCHECK,
                _cfg.Mic.KeepVolume, 0);

    // Setup sound combo boxes
    DialogControls::PopulateSoundCombo(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO),
                                       _cfg.RecentSounds, _cfg.Mic.MuteSound);
    DialogControls::PopulateSoundCombo(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO),
                                       _cfg.RecentSounds, _cfg.Mic.UnmuteSound);
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
        .ArgsLabel = desc && !runsCommand ? std::string{desc->ArgsLabel} : "",
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


void SettingsWindowViewModel::InitializeAboutSection(HWND hWnd) {
    SetDlgItemTextW(hWnd, IDC_ABOUT_VERSION_INFO, Str::Utf8ToWide("Version " + g_AppVersion.GetFullFormat()).c_str());

    // Underlined copy of the dialog font, so the link scales with the rest of the page.
    // Created once: the page is destroyed and rebuilt on every visit to this category.
    if (!_linkFont) {
        LOGFONTW logFont{};
        GetObjectW((HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0), sizeof(logFont), &logFont);
        logFont.lfUnderline = TRUE;
        _linkFont = CreateFontIndirectW(&logFont);
    }

    SendMessage(GetDlgItem(hWnd, IDC_ABOUT_GITHUB_LINK), WM_SETFONT, (WPARAM)_linkFont, TRUE);

    SetupLogDisplay(hWnd);
}

void SettingsWindowViewModel::SetupLogDisplay(HWND hWnd) {
    if (!GetDlgItem(hWnd, IDC_ABOUT_LOG_LIST)) {
        return;
    }

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

bool SettingsWindowViewModel::RequestElevationFor(HWND hWnd, const wchar_t* feature) const {
    const std::wstring message = std::wstring(L"Administrator privileges are required for ") + feature
        + L".\nWould you like to restart the application as administrator?";

    if (MessageBoxW(hWnd, message.c_str(), L"Administrator Required", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return false;
    }

    // Restarting elevated is the commit: the elevated instance reads the file, so the choice has
    // to survive the process it was made in. RequestElevation does not return when it succeeds.
    _cfg.Save();
    return UAC::RequestElevation();
}

/// Applies the settings that reach outside the config file. Called from Apply, never from a click,
/// so closing the window with Cancel leaves the registry and the task scheduler untouched.
void SettingsWindowViewModel::CommitPrivilegedSettings() const {
    if (_autoStartRequested != _autoStartInitial) {
        _autoStartRequested ? Registry::AddToAutoStartup(APP_NAME)
                            : Registry::RemoveFromAutoStartup(APP_NAME);
    }

    if (_cfg.Core.SkipUac == _cfgPrev.Core.SkipUac) {
        return;
    }

    if (_cfg.Core.SkipUac ? UAC::EnableSkipUAC() : UAC::DisableSkipUAC()) {
        return;
    }

    MessageBoxW(_view->GetHandle(),
                _cfg.Core.SkipUac ? L"Failed to create UAC bypass task."
                                      : L"Failed to remove UAC bypass task.",
                L"Error", MB_OK | MB_ICONERROR);
    _cfg.Core.SkipUac = _cfgPrev.Core.SkipUac;
}

void SettingsWindowViewModel::HandleButtonClick(HWND hWnd, int buttonId) {
    switch (buttonId) {
        case IDC_SETTINGS_AUTOSTART:
            _autoStartRequested = DialogControls::IsChecked(hWnd, buttonId);
            break;

        case IDC_SETTINGS_SKIP_UAC: {
            const bool requested = DialogControls::IsChecked(hWnd, buttonId);

            if (!UAC::IsElevated()) {
                _cfg.Core.SkipUac = requested;
                if (!RequestElevationFor(hWnd, L"UAC bypass")) {
                    // Declined, or the OS refused - put the file back the way we found it
                    _cfg.Core.SkipUac = !requested;
                    _cfg.Save();
                    SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, !requested, 0);
                }
                return;
            }

            _cfg.Core.SkipUac = requested;
            break;
        }

        case IDC_SETTINGS_UPDATES_ENABLED: {
            _cfg.Core.Updates = DialogControls::IsChecked(hWnd, buttonId);

            // Auto-update cannot outlive the check that feeds it
            if (!_cfg.Core.Updates) {
                _cfg.Core.AutoUpdate = false;
                SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), BM_SETCHECK, FALSE, 0);
            }

            EnableWindow(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), _cfg.Core.Updates);
            break;
        }
        case IDC_SETTINGS_AUTO_UPDATE_ENABLED:
            _cfg.Core.AutoUpdate = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_INDICATOR_CAPTURE:
            _cfg.Indicator.ExcludeFromCapture = DialogControls::IsChecked(hWnd, buttonId);
            break;

        case IDC_SETTINGS_INDICATOR_ON_TOP: {
            const bool requested = DialogControls::IsChecked(hWnd, buttonId);

            if (requested && !UAC::IsElevated()) {
                _cfg.Indicator.OnTopExclusive = true;
                if (!RequestElevationFor(hWnd, L"the 'On top of all windows' feature")) {
                    // Declined, or the OS refused - put the file back the way we found it
                    _cfg.Indicator.OnTopExclusive = false;
                    _cfg.Save();
                    SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, FALSE, 0);
                }
                return;
            }

            _cfg.Indicator.OnTopExclusive = requested;
            break;
        }

        case IDC_SETTINGS_INDICATOR_HIDE_INACTIVE:
            _cfg.Indicator.HideWhenInactive = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_INDICATOR_NOTIFICATIONS:
            _cfg.Core.Notifications = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME:
            _cfg.Mic.KeepVolume = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_SOUNDS_MUTE_BROWSE:
            HandleSoundBrowse(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO, "Select mute sound file", _cfg.Mic.MuteSound);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_BROWSE:
            HandleSoundBrowse(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO, "Select unmute sound file", _cfg.Mic.UnmuteSound);
            break;
        case IDC_ABOUT_GITHUB_LINK:
            ShellExecuteW(nullptr, L"open", Str::Utf8ToWide(REPO_URL).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        default:
            break;
    }
}

void SettingsWindowViewModel::HandleComboBoxChange(HWND hWnd, int comboBoxId) const {
    switch (comboBoxId) {
        case IDC_SETTINGS_INDICATOR_COMBO:
            _cfg.Indicator.State = static_cast<IndicatorState>(
                SendMessage(GetDlgItem(hWnd, comboBoxId), CB_GETCURSEL, 0, 0));
            break;
        case IDC_SETTINGS_SOUNDS_MUTE_COMBO:
            HandleSoundSelection(hWnd, comboBoxId, _cfg.Mic.MuteSound);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_COMBO:
            HandleSoundSelection(hWnd, comboBoxId, _cfg.Mic.UnmuteSound);
            break;
        default:
            break;
    }
}

void SettingsWindowViewModel::HandleTrackbarChange(HWND hWnd, int trackbarId, int value) const {
    switch (trackbarId) {
        case IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR:
            _cfg.Indicator.Size = static_cast<BYTE>(value);
            _mainWindow->Relayout();
            break;
        case IDC_HOTKEYS_WINDOW_TRACKBAR:
            _cfg.Core.MultiPressWindowMs = static_cast<uint16_t>(value);
            break;
        case IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR:
            _cfg.Indicator.VolumeThreshold = static_cast<float>(value) / 100.0f;
            break;
        case IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR:
            _cfg.Mic.Volume = static_cast<int8_t>(value);
            break;
        case IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR:
            _cfg.Mic.BellVolume = static_cast<int8_t>(value);
            break;
        default:
            break;
    }
}

void SettingsWindowViewModel::HandleSoundSelection(HWND hWnd, int comboBoxId, std::string& configSource) const {
    configSource = DialogControls::ResolveSound(GetDlgItem(hWnd, comboBoxId), _cfg.RecentSounds);
}

void SettingsWindowViewModel::HandleSoundBrowse(HWND hWnd, int comboBoxId, const char* title,
                                                std::string& configSource) const {
    std::string selectedFile;
    if (!AudioFileValidator::PickValidWavFile(_view->GetHandle(), title, selectedFile)) {
        return;
    }

    configSource = selectedFile;
    DialogControls::AddRecentSound(_cfg.RecentSounds, selectedFile);
    DialogControls::PopulateSoundCombo(GetDlgItem(hWnd, comboBoxId), _cfg.RecentSounds, configSource);
}

void SettingsWindowViewModel::Init() {
    _current = this;
    _cfgPrev = _cfg;
    _autoStartInitial = Registry::IsInAutoStartup(APP_NAME);
    _autoStartRequested = _autoStartInitial;

    _mainWindow = static_cast<MainWindow*>(_view->GetParent());
    _mainWindow->Show();
    _mainWindow->ToggleInteractivity(true);

    _view->OnButtonClick = [this](HWND hWnd, int buttonId) { HandleButtonClick(hWnd, buttonId); };
    _view->OnComboBoxChange = [this](HWND hWnd, int comboBoxId) { HandleComboBoxChange(hWnd, comboBoxId); };
    _view->OnTrackbarChange = [this](HWND hWnd, int trackbarId, int value) {
        HandleTrackbarChange(hWnd, trackbarId, value);
    };
    _view->OnActionActivated = [this](int rowIndex) { HandleActionActivated(rowIndex); };

    _view->OnApply += [this] {
        _mainWindow->UpdateRect();
        _cfg.Indicator.PosX = _mainWindow->GetPositionX();
        _cfg.Indicator.PosY = _mainWindow->GetPositionY();

        CommitPrivilegedSettings();

        if (_cfg != _cfgPrev) {
            _cfg.Save();
            _cfgPrev = _cfg;
        }
        _autoStartInitial = _autoStartRequested;
    };

    _view->OnExit += [this] {
        _mainWindow->ToggleInteractivity(false);
        _cfg = _cfgPrev;
    };
}
