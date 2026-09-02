#include "SettingsWindowViewModel.hpp"
#include "HotkeyCapture.hpp"
#include "Core/HotkeyService.hpp"
#include "Core/KeyNames.hpp"
#include "MainWindow/MainWindow.hpp"
#include "Resources/Resource.h"
#include "definitions.h"
#include "Lib/Version.hpp"
#include "Lib/UACService.hpp"
#include <windows.h>
#include <commctrl.h>

void SettingsWindowViewModel::HandleSectionChange(HWND hWnd, int sectionId) {
    switch (sectionId) {
        case IDD_SETTINGS_GENERAL:
            InitializeGeneralSection(hWnd);
            break;
        case IDD_SETTINGS_INDICATOR:
            InitializeIndicatorSection(hWnd);
            break;
        case IDD_SETTINGS_SOUNDS:
            InitializeSoundsSection(hWnd);
            break;
        case IDD_SETTINGS_HOTKEYS:
            InitializeHotkeysSection(hWnd);
            break;
        case IDD_SETTINGS_ABOUT:
            InitializeAboutSection(hWnd);
            break;
        default:
            break;
    }
}

void SettingsWindowViewModel::InitializeHotkeysSection(HWND hWnd) const {
    RefreshActionRows();
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_HOTKEYS_WINDOW_TRACKBAR),
                                 10, MAKELONG(100, 600), _cfg.MultiPressWindowMs);
}

void SettingsWindowViewModel::InitializeGeneralSection(HWND hWnd) const {
    // Rendered from the pending state, not from the registry: leaving the page and coming back
    // must not throw away a toggle that has not been applied yet
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTOSTART), BM_SETCHECK, _autoStartRequested, 0);

    // Left enabled without elevation on purpose - clicking it is what offers the restart
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_SKIP_UAC), BM_SETCHECK, _cfg.IsSkipUACEnabled, 0);

    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_UPDATES_ENABLED), BM_SETCHECK,
                _cfg.IsUpdatesEnabled, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), BM_SETCHECK,
                _cfg.IsAutoUpdateEnabled, 0);
    EnableWindow(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), _cfg.IsUpdatesEnabled);
}

void SettingsWindowViewModel::InitializeIndicatorSection(HWND hWnd) const {
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_CAPTURE), BM_SETCHECK,
                _cfg.ExcludeFromCapture, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_ON_TOP), BM_SETCHECK,
                _cfg.OnTopExclusive, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_HIDE_INACTIVE), BM_SETCHECK,
                _cfg.HideWhenInactive, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_NOTIFICATIONS), BM_SETCHECK,
                _cfg.NotificationsEnabled, 0);

    // Setup indicator state combo box
    HWND hCombo = GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_COMBO);
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    for (const auto& state : IndicatorStates) {
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)state);
    }
    SendMessage(hCombo, CB_SETCURSEL, (WPARAM)_cfg.IndicatorState, 0);

    // Setup trackbars
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR),
                       1, MAKELONG(10, 32), _cfg.IndicatorSize);
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR),
                       1, MAKELONG(0, 100), static_cast<int>(_cfg.IndicatorVolumeThreshold * 100));
}

void SettingsWindowViewModel::InitializeSoundsSection(HWND hWnd) const {
    // -1 means "never set", so the slider opens on whatever the device is actually at
    const int micVolume = _cfg.MicVolume == -1 ? _audioManager.CaptureDevice()->GetVolumePercent()
                                               : _cfg.MicVolume;
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), micVolume);

    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), _cfg.BellVolume);

    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME), BM_SETCHECK,
                _cfg.IsMicKeepVolume, 0);

    // Setup sound combo boxes
    DialogControls::PopulateSoundCombo(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO),
                                       _cfg.RecentSounds, _cfg.MuteSoundSource);
    DialogControls::PopulateSoundCombo(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO),
                                       _cfg.RecentSounds, _cfg.UnmuteSoundSource);
}
/// One row per action in config order, then the row that adds another.
void SettingsWindowViewModel::RefreshActionRows() const {
    const auto describe = [](const uint64_t mask, const bool onRelease, const uint8_t presses) {
        std::string hotkey = mask ? KeyNames::Format(mask) : "";
        if (hotkey.empty()) {
            return hotkey;
        }

        if (presses > 1) {
            hotkey += " x" + std::to_string(presses);
        }
        if (onRelease) {
            hotkey += " (release)";
        }
        return hotkey;
    };

    std::vector<ActionRow> rows;
    rows.reserve(_cfg.Actions.size() + 1);

    for (const auto& action : _cfg.Actions) {
        const ActionDesc* const desc = ActionRegistry::Find(action.ActionId);
        rows.push_back({
            .Name = action.Name,
            .Hotkey = describe(action.Hotkey, action.OnRelease, action.Presses),
            // The last column is the action's own argument, whatever that action reads it as
            .Command = action.Args,
            .RunsCommand = desc && HasFlag(desc->Flags, ActionFlags::RunsCommand)
        });
    }

    rows.push_back({.Name = "+ Add action..."});
    _view->SetActionRows(rows);
}

/// Frees a combination from every other action. What has to be unique is the pair: the same
/// combination may drive several actions as long as each wants a different number of presses.
void SettingsWindowViewModel::ClearHotkey(uint64_t mask, uint8_t presses, int exceptIndex) {
    if (!mask) {
        return;
    }

    for (int i = 0; i < static_cast<int>(_cfg.Actions.size()); i++) {
        if (i != exceptIndex && _cfg.Actions[i].Hotkey == mask
            && _cfg.Actions[i].Presses == presses) {
            _cfg.Actions[i].Hotkey = 0;
        }
    }
}

void SettingsWindowViewModel::HandleActionActivated(int rowIndex) {
    if (rowIndex < static_cast<int>(_cfg.Actions.size())) {
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
    EditAction(static_cast<int>(_cfg.Actions.size()),
               {.Name = std::string{desc.Title},
                .ActionId = std::string{desc.Id},
                .Sound = std::string{desc.DefaultSound}});
}

/// An index past the end is a new action, and then the seed says which action it runs.
void SettingsWindowViewModel::EditAction(int index, const Action& seed) {
    const bool isExisting = index < static_cast<int>(_cfg.Actions.size());
    const Action stored = isExisting ? _cfg.Actions[index] : seed;
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
        .Hotkey = stored.Hotkey,
        .OnRelease = stored.OnRelease,
        .Presses = stored.Presses,
        .Block = stored.Block,
        .TapOnly = stored.TapOnly,
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
        _cfg.Actions.erase(_cfg.Actions.begin() + index);
        return;
    }

    ClearHotkey(edit.Hotkey, edit.Presses, isExisting ? index : -1);

    // Starts from what was stored so the action id the entry points at survives the edit
    Action action = stored;
    action.Name = edit.Name;
    action.Args = runsCommand ? edit.Command : edit.Args;
    action.Sound = edit.Sound;
    action.Notification = edit.Notification;
    action.Hotkey = edit.Hotkey;
    action.OnRelease = edit.OnRelease;
    action.Presses = edit.Presses;
    action.Block = edit.Block;
    action.TapOnly = edit.TapOnly;
    action.SoundVolume = edit.SoundVolume;
    action.ShowNotification = edit.ShowNotification;

    if (isExisting) {
        _cfg.Actions[index] = action;
    } else {
        _cfg.Actions.push_back(action);
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

    if (_cfg.IsSkipUACEnabled == _cfgPrev.IsSkipUACEnabled) {
        return;
    }

    if (_cfg.IsSkipUACEnabled ? UAC::EnableSkipUAC() : UAC::DisableSkipUAC()) {
        return;
    }

    MessageBoxW(_view->GetHandle(),
                _cfg.IsSkipUACEnabled ? L"Failed to create UAC bypass task."
                                      : L"Failed to remove UAC bypass task.",
                L"Error", MB_OK | MB_ICONERROR);
    _cfg.IsSkipUACEnabled = _cfgPrev.IsSkipUACEnabled;
}

void SettingsWindowViewModel::HandleButtonClick(HWND hWnd, int buttonId) {
    switch (buttonId) {
        case IDC_SETTINGS_AUTOSTART:
            _autoStartRequested = DialogControls::IsChecked(hWnd, buttonId);
            break;

        case IDC_SETTINGS_SKIP_UAC: {
            const bool requested = DialogControls::IsChecked(hWnd, buttonId);

            if (!UAC::IsElevated()) {
                _cfg.IsSkipUACEnabled = requested;
                if (!RequestElevationFor(hWnd, L"UAC bypass")) {
                    // Declined, or the OS refused - put the file back the way we found it
                    _cfg.IsSkipUACEnabled = !requested;
                    _cfg.Save();
                    SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, !requested, 0);
                }
                return;
            }

            _cfg.IsSkipUACEnabled = requested;
            break;
        }

        case IDC_SETTINGS_UPDATES_ENABLED: {
            _cfg.IsUpdatesEnabled = DialogControls::IsChecked(hWnd, buttonId);

            // Auto-update cannot outlive the check that feeds it
            if (!_cfg.IsUpdatesEnabled) {
                _cfg.IsAutoUpdateEnabled = false;
                SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), BM_SETCHECK, FALSE, 0);
            }

            EnableWindow(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), _cfg.IsUpdatesEnabled);
            break;
        }
        case IDC_SETTINGS_AUTO_UPDATE_ENABLED:
            _cfg.IsAutoUpdateEnabled = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_INDICATOR_CAPTURE:
            _cfg.ExcludeFromCapture = DialogControls::IsChecked(hWnd, buttonId);
            break;

        case IDC_SETTINGS_INDICATOR_ON_TOP: {
            const bool requested = DialogControls::IsChecked(hWnd, buttonId);

            if (requested && !UAC::IsElevated()) {
                _cfg.OnTopExclusive = true;
                if (!RequestElevationFor(hWnd, L"the 'On top of all windows' feature")) {
                    // Declined, or the OS refused - put the file back the way we found it
                    _cfg.OnTopExclusive = false;
                    _cfg.Save();
                    SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, FALSE, 0);
                }
                return;
            }

            _cfg.OnTopExclusive = requested;
            break;
        }

        case IDC_SETTINGS_INDICATOR_HIDE_INACTIVE:
            _cfg.HideWhenInactive = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_INDICATOR_NOTIFICATIONS:
            _cfg.NotificationsEnabled = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME:
            _cfg.IsMicKeepVolume = DialogControls::IsChecked(hWnd, buttonId);
            break;
        case IDC_SETTINGS_SOUNDS_MUTE_BROWSE:
            HandleSoundBrowse(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO, "Select mute sound file", _cfg.MuteSoundSource);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_BROWSE:
            HandleSoundBrowse(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO, "Select unmute sound file", _cfg.UnmuteSoundSource);
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
            _cfg.IndicatorState = static_cast<IndicatorState>(
                SendMessage(GetDlgItem(hWnd, comboBoxId), CB_GETCURSEL, 0, 0));
            break;
        case IDC_SETTINGS_SOUNDS_MUTE_COMBO:
            HandleSoundSelection(hWnd, comboBoxId, _cfg.MuteSoundSource);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_COMBO:
            HandleSoundSelection(hWnd, comboBoxId, _cfg.UnmuteSoundSource);
            break;
        default:
            break;
    }
}

void SettingsWindowViewModel::HandleTrackbarChange(HWND hWnd, int trackbarId, int value) const {
    switch (trackbarId) {
        case IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR:
            _cfg.IndicatorSize = static_cast<BYTE>(value);
            _mainWindow->Relayout();
            break;
        case IDC_HOTKEYS_WINDOW_TRACKBAR:
            _cfg.MultiPressWindowMs = static_cast<uint16_t>(value);
            break;
        case IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR:
            _cfg.IndicatorVolumeThreshold = static_cast<float>(value) / 100.0f;
            break;
        case IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR:
            _cfg.MicVolume = static_cast<int8_t>(value);
            break;
        case IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR:
            _cfg.BellVolume = static_cast<int8_t>(value);
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
    _view->OnSectionChange = [this](HWND hWnd, int sectionId) { HandleSectionChange(hWnd, sectionId); };
    _view->OnActionActivated = [this](int rowIndex) { HandleActionActivated(rowIndex); };

    _view->OnApply += [this] {
        _mainWindow->UpdateRect();
        _cfg.WindowPosX = _mainWindow->GetPositionX();
        _cfg.WindowPosY = _mainWindow->GetPositionY();

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
