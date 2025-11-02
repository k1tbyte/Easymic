//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#define EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#include "AudioManager.hpp"
#include "Utils.hpp"
#include "ViewModel.hpp"
#include "SettingsWindow/SettingsWindow.hpp"
#include "View/Core/BaseWindow.hpp"

class SettingsWindowViewModel final : public BaseViewModel<SettingsWindow> {
private:
    const AudioManager &_audioManager;
    const AppConfig& _cfg;
    HWND MainWindowHandle = nullptr;

    constexpr static const char* IndicatorStates[] = {
        "Hidden", "Muted", "Muted or talking"
    };

public:
    SettingsWindowViewModel(const std::shared_ptr<BaseWindow>& baseView, const AppConfig& config, const AudioManager& audioManager) :
        BaseViewModel(baseView),
        _cfg(config),
        _audioManager(audioManager) {
    }

    void HandleSectionChange (HWND hWnd, int sectionId) {

#define Set(controlId, prop, value, param) \
        SendMessage(GetDlgItem(hWnd, controlId), prop, value, param)

        switch (sectionId) {
            case IDD_SETTINGS_GENERAL: {
                DWORD affinity;
                GetWindowDisplayAffinity(MainWindowHandle, &affinity);

                Set(IDC_SETTINGS_AUTOSTART, BM_SETCHECK, Utils::IsInAutoStartup(AppName), 0);
                Set(IDC_SETTINGS_EXCLUDE_CAPTURE, BM_SETCHECK, affinity == WDA_EXCLUDEFROMCAPTURE, 0);
                for (const auto& state : IndicatorStates) {
                    Set(IDC_SETTINGS_INDICATOR_COMBO,CB_ADDSTRING, 0, (LPARAM)state);
                }
                Set(IDC_SETTINGS_INDICATOR_COMBO, CB_SETCURSEL, (WPARAM)_cfg.indicator, 0);
                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR),
                    1, MAKELONG(10, 32),32
                );
                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR),
                    1,MAKELONG(0, 100), 50
                );
                break;
            }


            case IDD_SETTINGS_SOUNDS:
                // Handle Hotkeys section activation
                printf("Hotkeys section activated\n");
                break;
        }
    }

    void Init() override {

        MainWindowHandle = _view->GetParent()->GetHandle();

        _view->OnButtonClick = [this](int buttonId) {
            switch (buttonId) {
                case IDC_SETTINGS_AUTOSTART:
                    Utils::IsInAutoStartup(AppName) ? Utils::RemoveFromAutoStartup(AppName) :
                        Utils::AddToAutoStartup(AppName);
                    break;
            }
            printf("Button clicked in Settings Window: ID=%d\n", buttonId);
        };
        _view->OnComboBoxChange = [this](int comboBoxId, int value) {
            printf("ComboBox changed in Settings Window: ID=%d, Value=%d\n", comboBoxId, value);
        };
        _view->OnTrackbarChange = [this](int trackbarId, int value) {
            printf("Trackbar changed in Settings Window: ID=%d, Value=%d\n", trackbarId, value);
        };
        _view->OnSectionChange = [this](HWND hWnd, int sectionId) {
            this->HandleSectionChange(hWnd, sectionId);
        };
        // Initialize ViewModel logic here
    }
};
#endif //EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
