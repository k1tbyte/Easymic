#include "SettingsWindowViewModel.hpp"
#include "HotkeyManager.hpp"
#include "MainWindow/MainWindow.hpp"
#include "Resources/Resource.h"
#include "definitions.h"
#include "Lib/Version.hpp"
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

void SettingsWindowViewModel::InitializeGeneralSection(HWND hWnd) {
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_AUTOSTART), BM_SETCHECK, 
                Utils::IsInAutoStartup(AppName), 0);
}

void SettingsWindowViewModel::InitializeIndicatorSection(HWND hWnd) {
    DWORD affinity;
    GetWindowDisplayAffinity(MainWnd->GetEffectiveHandle(), &affinity);
    
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_CAPTURE), BM_SETCHECK, 
                affinity == WDA_EXCLUDEFROMCAPTURE, 0);
    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_ON_TOP), BM_SETCHECK, 
                _cfg.OnTopExclusive, 0);
    
    // Setup indicator state combo box
    HWND hCombo = GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_COMBO);
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    
    for (const auto& state : IndicatorStates) {
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)state);
    }
    SendMessage(hCombo, CB_SETCURSEL, (WPARAM)_cfg.IndicatorState, 0);

    // Setup trackbars
    Utils::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR),
                       1, MAKELONG(10, 32), _cfg.IndicatorSize);
    Utils::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR),
                       1, MAKELONG(0, 100), static_cast<int>(_cfg.IndicatorVolumeThreshold * 100));
}

void SettingsWindowViewModel::InitializeSoundsSection(HWND hWnd) {
    // Initialize volume trackbars with proper fallback values
    int micVolume = (_cfg.MicVolume == -1) ? 
                    _audioManager.CaptureDevice()->GetVolumePercent() : _cfg.MicVolume;
    Utils::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), micVolume);

    int bellVolume = (_cfg.BellVolume == -1) ? 
                     _audioManager.PlaybackDevice()->GetSimpleVolumePercent() : _cfg.BellVolume;
    Utils::InitTrackbar(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR),
                       1, MAKELONG(0, 100), bellVolume);

    SendMessage(GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME), BM_SETCHECK, 
                _cfg.IsMicKeepVolume, 0);

    // Setup sound combo boxes
    UpdateSoundComboBox(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO, 
                       _cfg.MuteSoundRecentSources, _cfg.MuteSoundSource);
    UpdateSoundComboBox(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO, 
                       _cfg.UnmuteSoundRecentSources, _cfg.UnmuteSoundSource);
}

void SettingsWindowViewModel::InitializeHotkeysSection(HWND hWnd) {
    const auto *hotkeyPtr = reinterpret_cast<char**>(&HotkeyTitles);
    for (int i = 0; i < sizeof(HotkeyTitles) / sizeof(char*); i++) {
        const auto& title = hotkeyPtr[i];
        auto it = _cfg.Hotkeys.find(std::string(title));
        if (it != _cfg.Hotkeys.end()) {
            _view->SetHotkeyCellValue(i, HotkeyManager::GetHotkeyName(it->second).c_str());
        }
    }
}

void SettingsWindowViewModel::InitializeAboutSection(HWND hWnd) {
    currentAboutHwnd_ = hWnd;
    
    // Set dynamic version information
    std::string version = "Version " + g_AppVersion.GetFullFormat();
    SetWindowTextA(GetDlgItem(hWnd, IDC_ABOUT_VERSION_INFO), version.c_str());
    
    // Make GitHub link look like a hyperlink (underlined)
    HWND hGithubLink = GetDlgItem(hWnd, IDC_ABOUT_GITHUB_LINK);
    HFONT hFont = CreateFont(
        -11, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, // TRUE for underline
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Ms Shell Dlg"));
    SendMessage(hGithubLink, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    SetupLogDisplay(hWnd);
}

void SettingsWindowViewModel::SetupLogDisplay(HWND hWnd) {
    HWND hLogEdit = GetDlgItem(hWnd, IDC_ABOUT_LOG_LIST);
    if (!hLogEdit) return;
    
    CleanupLogDisplay();
    
    // Load existing log content
    std::string logText = Logger::GetLogText();
    SetWindowTextA(hLogEdit, logText.c_str());
    
    // Scroll to bottom
    int textLength = GetWindowTextLengthA(hLogEdit);
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
                std::string logText = Logger::GetLogText();
                SetWindowTextA(hLogEdit, logText.c_str());
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
    
    int textLength = GetWindowTextLengthA(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, textLength, textLength);
    
    std::string newLine = (textLength > 0 ? "\r\n" : "") + formattedEntry;
    SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)newLine.c_str());
    
    // Scroll to bottom
    textLength = GetWindowTextLengthA(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, textLength, textLength);
    SendMessage(hLogEdit, EM_SCROLLCARET, 0, 0);
}

void SettingsWindowViewModel::HandleButtonClick(HWND hWnd, int buttonId) {
    switch (buttonId) {
        case IDC_SETTINGS_AUTOSTART:
            if (Utils::IsInAutoStartup(AppName)) {
                Utils::RemoveFromAutoStartup(AppName);
            } else {
                Utils::AddToAutoStartup(AppName);
            }
            break;
        case IDC_SETTINGS_INDICATOR_CAPTURE:
            _cfg.ExcludeFromCapture = Utils::IsCheckboxCheck(hWnd, buttonId);
            break;
        case IDC_SETTINGS_INDICATOR_ON_TOP:
            _cfg.OnTopExclusive = Utils::IsCheckboxCheck(hWnd, buttonId);
            break;
        case IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME:
            _cfg.IsMicKeepVolume = Utils::IsCheckboxCheck(hWnd, buttonId);
            break;
        case IDC_SETTINGS_SOUNDS_MUTE_BROWSE:
            HandleSoundFileBrowse(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO, "Select mute sound file",
                                _cfg.MuteSoundSource, _cfg.MuteSoundRecentSources);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_BROWSE:
            HandleSoundFileBrowse(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO, "Select unmute sound file",
                                _cfg.UnmuteSoundSource, _cfg.UnmuteSoundRecentSources);
            break;
        case IDC_ABOUT_GITHUB_LINK:
            // Open GitHub link
            ShellExecuteA(nullptr, "open", REPO_URL, nullptr, nullptr, SW_SHOWNORMAL);
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
            HandleSoundSourceSelection(hWnd, comboBoxId, _cfg.MuteSoundSource, _cfg.MuteSoundRecentSources);
            break;
        case IDC_SETTINGS_SOUNDS_UNMUTE_COMBO:
            HandleSoundSourceSelection(hWnd, comboBoxId, _cfg.UnmuteSoundSource, _cfg.UnmuteSoundRecentSources);
            break;
    }
}

void SettingsWindowViewModel::HandleTrackbarChange(HWND hWnd, int trackbarId, int value) {
    switch (trackbarId) {
        case IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR:
            _cfg.IndicatorSize = static_cast<BYTE>(value);
            MainWnd->UpdateRect()
                ->SetWidth(value)
                ->SetHeight(value)
                ->RefreshPos(nullptr)
                ->Invalidate();
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

bool SettingsWindowViewModel::SelectSoundFile(HWND hWnd, const char* title, std::string& result) {
    std::string selectedFile = AudioFileValidator::ShowWavFileDialog(hWnd, title);
    if (selectedFile.empty()) {
        return false;
    }

    WavValidationResult validation = AudioFileValidator::ValidateWavFile(selectedFile, 3.0f);
    if (!validation.isValid) {
        std::string errorMsg = "Invalid WAV file: " + validation.errorMessage;
        MessageBoxA(hWnd, errorMsg.c_str(), "File Validation Error", MB_OK | MB_ICONERROR);
        return false;
    }

    result = selectedFile;
    return true;
}

void SettingsWindowViewModel::UpdateSoundComboBox(HWND hWnd, int comboBoxId, 
                                                 const std::set<std::string>& sources, 
                                                 const std::string& current) {
    Utils::PopulateSourceComboBox(GetDlgItem(hWnd, comboBoxId), sources, current);
}

void SettingsWindowViewModel::HandleSoundSourceSelection(HWND hWnd, int comboBoxId, 
                                                        std::string& configSource, 
                                                        std::set<std::string>& recentSources) {
    int selectedIndex = SendMessage(GetDlgItem(hWnd, comboBoxId), CB_GETCURSEL, 0, 0);

    if (selectedIndex == 0 || selectedIndex == CB_ERR) {
        configSource.clear();
        return;
    }

    std::vector<std::string> validSources;
    for (const auto& source : recentSources) {
        if (Utils::DoesFileExist(source)) {
            validSources.push_back(source);
        }
    }

    if (selectedIndex - 1 < validSources.size()) {
        configSource = validSources[selectedIndex - 1];
        return;
    }
    
    configSource.clear();
    Utils::CleanupInvalidSources(recentSources);
    UpdateSoundComboBox(hWnd, comboBoxId, recentSources, configSource);
}

bool SettingsWindowViewModel::HandleSoundFileBrowse(HWND hWnd, int comboBoxId, const char* title, 
                                                   std::string& configSource, 
                                                   std::set<std::string>& recentSources) {
    std::string selectedFile;
    if (!SelectSoundFile(this->_view->GetHandle(), title, selectedFile)) {
        return false;
    }
    
    configSource = selectedFile;
    Utils::AddToRecentSources(recentSources, selectedFile);
    UpdateSoundComboBox(hWnd, comboBoxId, recentSources, configSource);
    return true;
}

void SettingsWindowViewModel::HandleHotkeyBinding(HWND hWnd, int index, LPCSTR itemText) const {
    static uint64_t _prevSequenceMask = 0;

    if (_prevSequenceMask > 0) {
        return;
    }
    
    HotkeyManager::Initialize();
    _view->SetHotkeySectionTitle(L"Press desired key combination or ESC to clear...");

    HotkeyManager::BindStart([this, index, itemText](
        uint8_t vkCode,
        HotkeyManager::InputState state,
        uint64_t sequenceMask,
        const std::string& hotkeyName) {

        if (state != HotkeyManager::InputState::RELEASED) {
            _prevSequenceMask = sequenceMask;
            this->_view->SetHotkeyCellValue(index, hotkeyName.c_str());
            return;
        }

        if (vkCode == VK_ESCAPE && sequenceMask == 0) {
            _prevSequenceMask = 0;
            _cfg.Hotkeys.erase(std::string(itemText));
        } else if (_prevSequenceMask > 0) {
            for (const auto& [actionTitle, mask] : _cfg.Hotkeys) {
                if (mask == _prevSequenceMask) {
                    _view->ResetHotkeyCellValue(actionTitle.c_str());
                    _cfg.Hotkeys.erase(actionTitle);
                    break;
                }
            }
            _cfg.Hotkeys[std::string(itemText)] = _prevSequenceMask;
        }

        HotkeyManager::BindStop();
        HotkeyManager::Dispose();
        this->_view->SetHotkeyCellValue(index, HotkeyManager::GetHotkeyName(_prevSequenceMask).c_str());
        this->_view->SetHotkeySectionTitle(nullptr);
        _prevSequenceMask = 0;
    });
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

    _view->OnHotkeyListChange = [this](HWND hWnd, int index, LPCSTR itemText) {
        HandleHotkeyBinding(hWnd, index, itemText);
    };

    _view->OnApply += [this]() {
        MainWnd->UpdateRect();
        _cfg.WindowPosX = static_cast<USHORT>(MainWnd->GetPositionX());
        _cfg.WindowPosY = static_cast<USHORT>(MainWnd->GetPositionY());

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
