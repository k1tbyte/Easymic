#include "SettingsWindowViewModel.hpp"
#include "HotkeyCapture.hpp"
#include "HotkeyManager.hpp"
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

/// Built-ins first, in table order, then the custom ones, then the add row.
void SettingsWindowViewModel::RefreshActionRows() const {
    const auto describe = [](const uint64_t mask, const bool onRelease, const uint8_t presses) {
        std::string hotkey = mask ? HotkeyManager::GetHotkeyName(mask) : "";
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
    rows.reserve(BuiltInActions::Count + _cfg.CustomActions.size() + 1);

    for (const auto& builtIn : BuiltInActions::All) {
        const auto it = _cfg.Actions.find(builtIn.Key);
        const ActionBinding binding = it != _cfg.Actions.end() ? it->second : ActionBinding{};

        rows.push_back({
            .Name = builtIn.Title,
            .Hotkey = describe(binding.Hotkey, binding.OnRelease, binding.Presses),
        });
    }

    for (const auto& action : _cfg.CustomActions) {
        rows.push_back({
            .Name = action.Name,
            .Hotkey = describe(action.Hotkey, action.OnRelease, action.Presses),
            .Command = action.Command,
            .IsCustom = true
        });
    }

    rows.push_back({.Name = "+ Add action..."});
    _view->SetActionRows(rows);
}

/// Frees a combination from every other action. What has to be unique is the pair: the same
/// combination may drive several actions as long as each wants a different number of presses.
void SettingsWindowViewModel::ClearHotkey(uint64_t mask, uint8_t presses,
                                          const std::string& exceptBuiltIn, int exceptCustomIndex) {
    if (!mask) {
        return;
    }

    for (auto& [id, binding] : _cfg.Actions) {
        if (id != exceptBuiltIn && binding.Hotkey == mask && binding.Presses == presses) {
            binding.Hotkey = 0;
        }
    }

    for (int i = 0; i < static_cast<int>(_cfg.CustomActions.size()); i++) {
        if (i != exceptCustomIndex && _cfg.CustomActions[i].Hotkey == mask
            && _cfg.CustomActions[i].Presses == presses) {
            _cfg.CustomActions[i].Hotkey = 0;
        }
    }
}

void SettingsWindowViewModel::HandleActionActivated(int rowIndex) {
    if (rowIndex < BuiltInActions::Count) {
        EditBuiltInAction(BuiltInActions::All[rowIndex]);
    } else {
        EditCustomAction(rowIndex - BuiltInActions::Count);
    }

    RefreshActionRows();
}

void SettingsWindowViewModel::EditBuiltInAction(const BuiltInAction& builtIn) {
    const auto it = _cfg.Actions.find(builtIn.Key);
    const bool isConfigured = it != _cfg.Actions.end();

    ActionEdit edit{
        .Title = builtIn.Title,
        // An unconfigured action starts from the table default, so saving it as is keeps the
        // behaviour the user was promised by the list
        .Sound = isConfigured ? it->second.Sound : builtIn.DefaultSound,
        // The box always shows what will actually appear on screen, table default included
        .Notification = isConfigured && !it->second.Notification.empty() ? it->second.Notification
                                                                        : builtIn.DefaultNotification,
        .Hotkey = isConfigured ? it->second.Hotkey : 0,
        .OnRelease = isConfigured && it->second.OnRelease,
        .Presses = isConfigured ? it->second.Presses : uint8_t{1},
        .ShowNotification = !isConfigured || it->second.ShowNotification,
        .HasSound = builtIn.HasSound,
        .HoldOnly = builtIn.HoldOnly,
    };

    if (!_view->ShowActionDialog(edit, _cfg.RecentSounds)) {
        return;
    }

    ClearHotkey(edit.Hotkey, edit.Presses, builtIn.Key, -1);
    _cfg.Actions[builtIn.Key] = {.Hotkey = edit.Hotkey, .OnRelease = edit.OnRelease,
                                 .Presses = edit.Presses, .Sound = edit.Sound,
                                 .Notification = edit.Notification,
                                 .ShowNotification = edit.ShowNotification};
}

void SettingsWindowViewModel::EditCustomAction(int customIndex) {
    const bool isExisting = customIndex < static_cast<int>(_cfg.CustomActions.size());
    const CustomAction stored = isExisting ? _cfg.CustomActions[customIndex] : CustomAction{};

    ActionEdit edit{
        .Title = isExisting ? stored.Name : "New action",
        .Name = stored.Name,
        .Command = stored.Command,
        .Sound = stored.Sound,
        // The box always shows what will actually appear on screen, template included
        .Notification = stored.Notification.empty() ? BuiltInActions::DefaultNotification
                                                    : stored.Notification,
        .Hotkey = stored.Hotkey,
        .OnRelease = stored.OnRelease,
        .Presses = stored.Presses,
        .ShowNotification = stored.ShowNotification,
        .IsCustom = true,
        .AllowDelete = isExisting,
    };

    if (!_view->ShowActionDialog(edit, _cfg.RecentSounds)) {
        return;
    }

    if (edit.Deleted) {
        _cfg.CustomActions.erase(_cfg.CustomActions.begin() + customIndex);
        return;
    }

    ClearHotkey(edit.Hotkey, edit.Presses, {}, isExisting ? customIndex : -1);

    const CustomAction action{.Name = edit.Name, .Command = edit.Command, .Sound = edit.Sound,
                              .Notification = edit.Notification, .Hotkey = edit.Hotkey,
                              .OnRelease = edit.OnRelease, .Presses = edit.Presses,
                              .ShowNotification = edit.ShowNotification};

    if (isExisting) {
        _cfg.CustomActions[customIndex] = action;
    } else {
        _cfg.CustomActions.push_back(action);
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
