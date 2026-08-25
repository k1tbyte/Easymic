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
            RefreshActionRows();
            break;
        case IDD_SETTINGS_ABOUT:
            InitializeAboutSection(hWnd);
            break;
        default:
            break;
    }
}

void SettingsWindowViewModel::InitializeGeneralSection(HWND hWnd) {
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTOSTART), BM_SETCHECK, 
                Registry::IsInAutoStartup(APP_NAME), 0);

    // Skip UAC checkbox - only enable if running as admin
    HWND hSkipUAC = GetDlgItem(hWnd, IDC_SETTINGS_SKIP_UAC);
    SendMessage(hSkipUAC, BM_SETCHECK, _cfg.IsSkipUACEnabled, 0);
    EnableWindow(hSkipUAC, UAC::IsSkipUACAvailable());

    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_UPDATES_ENABLED), BM_SETCHECK,
                _cfg.IsUpdatesEnabled, 0);
}

void SettingsWindowViewModel::InitializeIndicatorSection(HWND hWnd) {
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

void SettingsWindowViewModel::InitializeSoundsSection(HWND hWnd) {
    // Initialize volume trackbars with proper fallback values
    int micVolume = (_cfg.MicVolume == -1) ? 
                    _audioManager.CaptureDevice()->GetVolumePercent() : _cfg.MicVolume;
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), micVolume);

    int bellVolume = (_cfg.BellVolume == -1) ? 
                     _audioManager.PlaybackDevice()->GetSimpleVolumePercent() : _cfg.BellVolume;
    DialogControls::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), bellVolume);

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
    const auto describe = [](const uint64_t mask, const bool onRelease) {
        std::string hotkey = mask ? HotkeyManager::GetHotkeyName(mask) : "";
        if (onRelease && !hotkey.empty()) {
            hotkey += " (release)";
        }
        return hotkey;
    };

    std::vector<ActionRow> rows;
    rows.reserve(BuiltInActions::Count + _cfg.CustomActions.size() + 1);

    for (const auto& builtIn : BuiltInActions::All) {
        const auto it = _cfg.Actions.find(builtIn.Id);
        const ActionBinding binding = it != _cfg.Actions.end() ? it->second : ActionBinding{};

        rows.push_back({
            .Name = builtIn.Title,
            .Hotkey = describe(binding.Hotkey, binding.OnRelease),
        });
    }

    for (const auto& action : _cfg.CustomActions) {
        rows.push_back({
            .Name = action.Name,
            .Hotkey = describe(action.Hotkey, action.OnRelease),
            .Command = action.Command,
            .IsCustom = true
        });
    }

    rows.push_back({.Name = "+ Add action..."});
    _view->SetActionRows(rows);
}

/// Frees a combination from every other action - one hotkey drives one action.
void SettingsWindowViewModel::ClearHotkey(uint64_t mask, const std::string& exceptBuiltIn, int exceptCustomIndex) {
    if (!mask) {
        return;
    }

    for (auto& [id, binding] : _cfg.Actions) {
        if (id != exceptBuiltIn && binding.Hotkey == mask) {
            binding.Hotkey = 0;
        }
    }

    for (int i = 0; i < static_cast<int>(_cfg.CustomActions.size()); i++) {
        if (i != exceptCustomIndex && _cfg.CustomActions[i].Hotkey == mask) {
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
    const auto it = _cfg.Actions.find(builtIn.Id);
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
        .ShowNotification = !isConfigured || it->second.ShowNotification,
        .HasSound = builtIn.HasSound,
        .HoldOnly = builtIn.HoldOnly,
    };

    if (!_view->ShowActionDialog(edit, _cfg.RecentSounds)) {
        return;
    }

    ClearHotkey(edit.Hotkey, builtIn.Id, -1);
    _cfg.Actions[builtIn.Id] = {.Hotkey = edit.Hotkey, .OnRelease = edit.OnRelease,
                                .Sound = edit.Sound, .Notification = edit.Notification,
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

    ClearHotkey(edit.Hotkey, {}, isExisting ? customIndex : -1);

    const CustomAction action{.Name = edit.Name, .Command = edit.Command, .Sound = edit.Sound,
                              .Notification = edit.Notification, .Hotkey = edit.Hotkey,
                              .OnRelease = edit.OnRelease, .ShowNotification = edit.ShowNotification};

    if (isExisting) {
        _cfg.CustomActions[customIndex] = action;
    } else {
        _cfg.CustomActions.push_back(action);
    }
}

void SettingsWindowViewModel::InitializeAboutSection(HWND hWnd) {
    currentAboutHwnd_ = hWnd;

    SetDlgItemTextW(hWnd, IDC_ABOUT_VERSION_INFO, Str::Utf8ToWide("Version " + g_AppVersion.GetFullFormat()).c_str());

    // Underlined copy of the dialog font, so the link scales with the rest of the page.
    // Created once: the page is destroyed and rebuilt on every visit to this category.
    if (!linkFont_) {
        LOGFONTW logFont{};
        GetObjectW((HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0), sizeof(logFont), &logFont);
        logFont.lfUnderline = TRUE;
        linkFont_ = CreateFontIndirectW(&logFont);
    }

    SendMessage(GetDlgItem(hWnd, IDC_ABOUT_GITHUB_LINK), WM_SETFONT, (WPARAM)linkFont_, TRUE);

    SetupLogDisplay(hWnd);
}

void SettingsWindowViewModel::SetupLogDisplay(HWND hWnd) {
    HWND hLogEdit = GetDlgItem(hWnd, IDC_ABOUT_LOG_LIST);
    if (!hLogEdit) return;
    
    CleanupLogDisplay();
    
    // Load existing log content
    SetWindowTextW(hLogEdit, Str::Utf8ToWide(Logger::GetLogText()).c_str());

    // Scroll to bottom
    int textLength = GetWindowTextLengthW(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, textLength, textLength);
    SendMessage(hLogEdit, EM_SCROLLCARET, 0, 0);
    
    // Subscribe to log events
    logAddedSubscriptionId_ = Logger::OnLogAdded += [this](Logger::Level level, const std::string& message, const std::string& formattedEntry) {
        UpdateLogDisplay(formattedEntry);
    };
    
    logClearedSubscriptionId_ = Logger::OnLogCleared += [this]() {
        if (currentAboutHwnd_) {
            HWND hLogEdit = GetDlgItem(currentAboutHwnd_, IDC_ABOUT_LOG_LIST);
            if (hLogEdit) {
                SetWindowTextW(hLogEdit, Str::Utf8ToWide(Logger::GetLogText()).c_str());
            }
        }
    };
}

void SettingsWindowViewModel::CleanupLogDisplay() {
    if (logAddedSubscriptionId_ != -1) {
        Logger::OnLogAdded -= logAddedSubscriptionId_;
        logAddedSubscriptionId_ = -1;
    }
    if (logClearedSubscriptionId_ != -1) {
        Logger::OnLogCleared -= logClearedSubscriptionId_;
        logClearedSubscriptionId_ = -1;
    }
}

void SettingsWindowViewModel::UpdateLogDisplay(const std::string& formattedEntry) {
    if (!currentAboutHwnd_) return;
    
    HWND hLogEdit = GetDlgItem(currentAboutHwnd_, IDC_ABOUT_LOG_LIST);
    if (!hLogEdit) return;
    
    int textLength = GetWindowTextLengthW(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, textLength, textLength);

    const std::wstring newLine = Str::Utf8ToWide((textLength > 0 ? "\r\n" : "") + formattedEntry);
    SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)newLine.c_str());

    // Scroll to bottom
    textLength = GetWindowTextLengthW(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, textLength, textLength);
    SendMessage(hLogEdit, EM_SCROLLCARET, 0, 0);
}

void SettingsWindowViewModel::HandleButtonClick(HWND hWnd, int buttonId) {
    switch (buttonId) {
        case IDC_SETTINGS_AUTOSTART:
            if (Registry::IsInAutoStartup(APP_NAME)) {
                Registry::RemoveFromAutoStartup(APP_NAME);
            } else {
                Registry::AddToAutoStartup(APP_NAME);
            }
            break;
        case IDC_SETTINGS_SKIP_UAC: {
            bool skipUACRequested = DialogControls::IsChecked(hWnd, buttonId);

            if (!UAC::IsElevated()) {
                // Show message and request elevation
                int result = MessageBoxW(hWnd,
                    L"Administrator privileges are required to configure UAC bypass.\nWould you like to restart the application as administrator?",
                    L"Administrator Required",
                    MB_YESNO | MB_ICONQUESTION);

                if (result == IDYES) {
                    // Save current config before elevation
                    _cfg.IsSkipUACEnabled = skipUACRequested;
                    _cfg.Save();

                    // Request elevation - this will close current instance
                    UAC::RequestElevation();
                    return;
                }
                // Revert checkbox state
                SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, !skipUACRequested, 0);
                return;
            }

            // Handle Skip UAC toggle
            bool success = false;
            if (skipUACRequested) {
                success = UAC::EnableSkipUAC();
                if (!success) {
                    MessageBoxW(hWnd, L"Failed to create UAC bypass task.", L"Error", MB_OK | MB_ICONERROR);
                }
            } else {
                success = UAC::DisableSkipUAC();
                if (!success) {
                    MessageBoxW(hWnd, L"Failed to remove UAC bypass task.", L"Error", MB_OK | MB_ICONERROR);
                }
            }

            // Update checkbox state based on actual result
            if (!success) {
                SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, !skipUACRequested, 0);
            } else {
                _cfg.IsSkipUACEnabled = skipUACRequested;
            }
            break;
        }
        case IDC_SETTINGS_UPDATES_ENABLED: {
            _cfg.IsUpdatesEnabled = DialogControls::IsChecked(hWnd, buttonId);

            // Disable auto-update if updates are disabled
            if (!_cfg.IsUpdatesEnabled) {
                _cfg.IsAutoUpdateEnabled = false;
                SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTO_UPDATE_ENABLED), BM_SETCHECK, FALSE, 0);
            }

            // Enable/disable auto-update checkbox based on updates enabled state
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
            bool onTopRequested = DialogControls::IsChecked(hWnd, buttonId);

            // Check if we need admin rights for "On top of all windows"
            if (onTopRequested && !UAC::IsElevated()) {
                // Show message and request elevation
                int result = MessageBoxW(hWnd,
                    L"Administrator privileges are required for 'On top of all windows' feature.\nWould you like to restart the application as administrator?",
                    L"Administrator Required",
                    MB_YESNO | MB_ICONQUESTION);

                if (result == IDYES) {
                    // Save current config before elevation
                    _cfg.OnTopExclusive = true;
                    _cfg.Save();

                    // Request elevation - this will close current instance
                    UAC::RequestElevation();
                    return;
                }
                SendMessage(GetDlgItem(hWnd, buttonId), BM_SETCHECK, FALSE, 0);

                return;
            }
            _cfg.OnTopExclusive = onTopRequested;
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
            // Open GitHub link
            ShellExecuteW(nullptr, L"open", Str::Utf8ToWide(REPO_URL).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
    }
}

void SettingsWindowViewModel::HandleComboBoxChange(HWND hWnd, int comboBoxId) {
    int indexSelected = SendMessage(GetDlgItem(hWnd, comboBoxId), CB_GETCURSEL, 0, 0);
    
    switch (comboBoxId) {
        case IDC_SETTINGS_INDICATOR_COMBO:
            _cfg.IndicatorState = static_cast<IndicatorState>(indexSelected);
            break;
        case IDC_SETTINGS_SOUNDS_MUTE_COMBO:
            HandleSoundSelection(hWnd, comboBoxId, _cfg.MuteSoundSource);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_COMBO:
            HandleSoundSelection(hWnd, comboBoxId, _cfg.UnmuteSoundSource);
            break;
    }
}

void SettingsWindowViewModel::HandleTrackbarChange(HWND hWnd, int trackbarId, int value) {
    switch (trackbarId) {
        case IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR:
            _cfg.IndicatorSize = static_cast<BYTE>(value);
            MainWnd->Relayout();
            break;
        case IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR:
            _cfg.IndicatorVolumeThreshold = static_cast<float>(value) / 100.0f;
            break;
        case IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR:
            _cfg.MicVolume = static_cast<BYTE>(value);
            break;
        case IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR:
            _cfg.BellVolume = static_cast<BYTE>(value);
            break;
    }
}

void SettingsWindowViewModel::HandleSoundSelection(HWND hWnd, int comboBoxId, std::string& configSource) {
    configSource = DialogControls::ResolveSound(GetDlgItem(hWnd, comboBoxId), _cfg.RecentSounds);
}

void SettingsWindowViewModel::HandleSoundBrowse(HWND hWnd, int comboBoxId, const char* title,
                                                std::string& configSource) {
    std::string selectedFile;
    if (!AudioFileValidator::PickValidWavFile(this->_view->GetHandle(), title, selectedFile)) {
        return;
    }

    configSource = selectedFile;
    DialogControls::AddRecentSound(_cfg.RecentSounds, selectedFile);
    DialogControls::PopulateSoundCombo(GetDlgItem(hWnd, comboBoxId), _cfg.RecentSounds, configSource);
}

void SettingsWindowViewModel::Init() {
    _cfgPrev = _cfg;
    MainWnd = reinterpret_cast<MainWindow *>(_view->GetParent());
    MainWnd->Show();
    MainWnd->ToggleInteractivity(true);

    _view->OnButtonClick = [this](HWND hWnd, int buttonId) {
        HandleButtonClick(hWnd, buttonId);
    };

    _view->OnComboBoxChange = [this](HWND hWnd, int comboBoxId) {
        HandleComboBoxChange(hWnd, comboBoxId);
    };

    _view->OnTrackbarChange = [this](HWND hWnd, int trackbarId, int value) {
        HandleTrackbarChange(hWnd, trackbarId, value);
    };

    _view->OnSectionChange = [this](HWND hWnd, int sectionId) {
        HandleSectionChange(hWnd, sectionId);
    };

    _view->OnActionActivated = [this](int rowIndex) {
        HandleActionActivated(rowIndex);
    };

    _view->OnApply += [this]() {
        MainWnd->UpdateRect();
        _cfg.WindowPosX = MainWnd->GetPositionX();
        _cfg.WindowPosY = MainWnd->GetPositionY();

        if (_cfg != _cfgPrev) {
            _cfg.Save();
            _cfgPrev = _cfg;
        }
    };
    
    _view->OnExit += [this]() {
        MainWnd->ToggleInteractivity(false);
        _cfg = _cfgPrev;
    };
}
