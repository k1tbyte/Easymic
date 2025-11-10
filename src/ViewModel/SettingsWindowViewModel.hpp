//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#define EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#include "AudioManager.hpp"
#include "Audio/AudioFileValidator.hpp"
#include "Utils.hpp"
#include "ViewModel.hpp"
#include "SettingsWindow/SettingsWindow.hpp"
#include "View/Core/BaseWindow.hpp"

class SettingsWindowViewModel final : public BaseViewModel<SettingsWindow> {
private:
    const AudioManager &_audioManager;
    AppConfig& _cfg;
    AppConfig _cfgPrev;
    MainWindow* MainWnd = nullptr;

    constexpr static const char* IndicatorStates[] = {
        "Hidden", "Muted", "Muted or talking"
    };

    // Helper function to handle sound source selection from combobox
    void HandleSoundSourceSelection(HWND hWnd, int comboBoxId, std::string& configSource, std::set<std::string>& recentSources) {
        int selectedIndex = SendMessage(GetDlgItem(hWnd, comboBoxId), CB_GETCURSEL, 0, 0);

        if (selectedIndex == 0 || selectedIndex == CB_ERR) {
            configSource.clear();
            return;
        }

        // Convert index to iterator (index 1 = first element in set)
        auto it = recentSources.begin();
        std::advance(it, selectedIndex - 1);

        if (it != recentSources.end() && Utils::DoesFileExist(*it)) {
            configSource = *it;
            return;
        }

        // File doesn't exist, clean up and refresh
        configSource.clear();
        Utils::CleanupInvalidSources(recentSources);
        RefreshSoundComboBox(hWnd, comboBoxId, recentSources, configSource);
    }

    // Helper function to refresh sound combobox
    void RefreshSoundComboBox(HWND hWnd, int comboBoxId, const std::set<std::string>& recentSources, const std::string& currentSource) {
        Utils::PopulateSourceComboBox(GetDlgItem(hWnd, comboBoxId), recentSources, currentSource);
    }

    // Helper function for sound file browsing and updating combobox
    bool HandleSoundFileBrowse(HWND hWnd, int comboBoxId, const char* title, std::string& configSource, std::set<std::string>& recentSources) {
        std::string selectedFile;
        if (HandleSoundFileSelection(this->_view->GetHandle(), title, selectedFile)) {
            configSource = selectedFile;
            Utils::AddToRecentSources(recentSources, selectedFile);
            RefreshSoundComboBox(hWnd, comboBoxId, recentSources, configSource);
            return true;
        }
        return false;
    }

    /*std::unordered_map<const char*, void(SettingsWindowViewModel::*)()> hotkeyHandlers = {
        { HotkeyTitles.ToggleMute, &SettingsWindowViewModel::HotkeyToggleMute }
    };*/


public:
    SettingsWindowViewModel(const std::shared_ptr<BaseWindow>& baseView,  AppConfig& config, const AudioManager& audioManager) :
        BaseViewModel(baseView),
        _audioManager(audioManager),
        _cfg(config) {
    }

    void HandleSectionChange (HWND hWnd, int sectionId) {

#define Set(controlId, prop, value, param) \
        SendMessage(GetDlgItem(hWnd, controlId), prop, value, param)

        switch (sectionId) {
            case IDD_SETTINGS_GENERAL: {

                Set(IDC_SETTINGS_AUTOSTART, BM_SETCHECK, Utils::IsInAutoStartup(AppName), 0);

                break;
            }

            case IDD_SETTINGS_INDICATOR: {
                DWORD affinity;
                GetWindowDisplayAffinity(MainWnd->GetEffectiveHandle(), &affinity);
                Set(IDC_SETTINGS_INDICATOR_CAPTURE, BM_SETCHECK, affinity == WDA_EXCLUDEFROMCAPTURE, 0);
                Set(IDC_SETTINGS_INDICATOR_ON_TOP, BM_SETCHECK, _cfg.OnTopExclusive, 0);
                Set(IDC_SETTINGS_INDICATOR_COMBO, CB_RESETCONTENT, 0, 0);

                // Add strings to combobox
                for (const auto& state : IndicatorStates) {
                    Set(IDC_SETTINGS_INDICATOR_COMBO, CB_ADDSTRING, 0, (LPARAM)state);
                }
                Set(IDC_SETTINGS_INDICATOR_COMBO, CB_SETCURSEL, (WPARAM)_cfg.IndicatorState, 0);

                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR),
                    1, MAKELONG(10, 32), _cfg.IndicatorSize
                );
                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR),
                    1, MAKELONG(0, 100), static_cast<int>(_cfg.IndicatorVolumeThreshold * 100)
                );
                break;
            }


            case IDD_SETTINGS_SOUNDS: {
                // Initialize microphone volume trackbar
                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_MIC_VOLUME_TRACKBAR),
                    1, MAKELONG(0, 100), _cfg.MicVolume == static_cast<uint8_t>(-1) ? 50 : _cfg.MicVolume
                );

                // Initialize bell volume trackbar
                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_SOUNDS_BELL_VOLUME_TRACKBAR),
                    1, MAKELONG(0, 100), _cfg.BellVolume
                );

                RefreshSoundComboBox(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO, _cfg.MuteSoundRecentSources, _cfg.MuteSoundSource);
                RefreshSoundComboBox(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO, _cfg.UnmuteSoundRecentSources, _cfg.UnmuteSoundSource);

                break;
            }

            case IDD_SETTINGS_HOTKEYS: {
                const auto *hotkeyPtr = reinterpret_cast<char**>(&HotkeyTitles);
                for (int i = 0; i < sizeof(HotkeyTitles) / sizeof(char*); i++) {
                    const auto& title = hotkeyPtr[i];
                    if (auto it = _cfg.Hotkeys.find(std::string(title)); it != _cfg.Hotkeys.end()) {
                        _view->SetHotkeyCellValue(i, HotkeyManager::GetHotkeyName(it->second).c_str());
                    }
                }
                break;
            }
            case IDD_SETTINGS_ABOUT:
                // Handle About section activation
                printf("About section activated\n");
                break;

            default:
                // Handle unknown section
                break;
        }
    }

    void HandleButtonClick(HWND hWnd, int buttonId) {
        switch (buttonId) {
            case IDC_SETTINGS_AUTOSTART:
                Utils::IsInAutoStartup(AppName) ? Utils::RemoveFromAutoStartup(AppName) :
                    Utils::AddToAutoStartup(AppName);
                break;
            case IDC_SETTINGS_INDICATOR_CAPTURE:
                _cfg.ExcludeFromCapture = Utils::IsCheckboxCheck(hWnd, buttonId);
                break;
            case IDC_SETTINGS_INDICATOR_ON_TOP:
                _cfg.OnTopExclusive = Utils::IsCheckboxCheck(hWnd, buttonId);
                break;
            case IDC_SETTINGS_SOUNDS_MIC_KEEP_VOLUME:
                // Handle microphone volume lock checkbox
                break;
            case IDC_SETTINGS_SOUNDS_MUTE_BROWSE:
                HandleSoundFileBrowse(hWnd, IDC_SETTINGS_SOUNDS_MUTE_COMBO, "Select mute sound file",
                    _cfg.MuteSoundSource, _cfg.MuteSoundRecentSources);
                break;
            case IDC_SETTINGS_SOUNDS_UNMUTE_BROWSE:
                HandleSoundFileBrowse(hWnd, IDC_SETTINGS_SOUNDS_UNMUTE_COMBO, "Select unmute sound file",
                    _cfg.UnmuteSoundSource, _cfg.UnmuteSoundRecentSources);
                break;
        }
    }

    void HandleComboBoxChange(HWND hWnd, int comboBoxId) {
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
            default:
                // Handle unknown combobox
                break;
        }
    }

    void HandleTrackbarChange(HWND hWnd, int trackbarId, int value) {
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
            default:
                // Handle unknown trackbar
                break;
        }
    }

    static bool HandleSoundFileSelection(HWND hWnd, const char* title, std::string& result) {
        std::string selectedFile = AudioFileValidator::ShowWavFileDialog(hWnd, title);

        if (selectedFile.empty()) {
            return false;
        }

        // Validate the WAV file
        WavValidationResult validation = AudioFileValidator::ValidateWavFile(selectedFile, 3.0f);

        if (!validation.isValid) {
            std::string errorMsg = "Invalid WAV file: " + validation.errorMessage;
            MessageBoxA(hWnd, errorMsg.c_str(), "File Validation Error", MB_OK | MB_ICONERROR);
            return false;
        }

        result = selectedFile;
        return true;
    }



    void HandleHotkeyBinding(HWND hWnd, int index, LPCSTR itemText) const {
        static uint64_t _prevSequenceMask = 0;

        if (_prevSequenceMask > 0) {
            // Already in binding mode
            return;
        }
        HotkeyManager::Initialize();

        _view->SetHotkeySectionTitle(L"Press desired key combination or ESC to clear...");

        HotkeyManager::BindStart([this, index, itemText](
            uint8_t vkCode,
            HotkeyManager::InputState state,
            uint64_t sequenceMask,
            const std::string& hotkeyName) {

            if (state == HotkeyManager::InputState::RELEASED) {


                if (vkCode == VK_ESCAPE && sequenceMask == 0) {
                    _prevSequenceMask = 0;
                    _cfg.Hotkeys.erase(std::string(itemText));
                }
                else if (_prevSequenceMask > 0) {
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
                return;
            }

            _prevSequenceMask = sequenceMask;
            this->_view->SetHotkeyCellValue(index, hotkeyName.c_str());
        });
    }

    void Init() override {
        _cfgPrev = _cfg;
        MainWnd = reinterpret_cast<MainWindow *>(_view->GetParent());


        MainWnd->Show();
        MainWnd->ToggleInteractivity(true);
        // Allow drag move on parent window by removing WS_EX_TRANSPARENT

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

            // Revert changes if needed
            _cfg = _cfgPrev;
        };
    }
};
#endif //EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
