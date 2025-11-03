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
    AppConfig& _cfg;
    AppConfig _cfgPrev;
    BaseWindow* MainWindow = nullptr;

    constexpr static const char* IndicatorStates[] = {
        "Hidden", "Muted", "Muted or talking"
    };

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
                GetWindowDisplayAffinity(MainWindow->GetEffectiveHandle(), &affinity);
                Set(IDC_SETTINGS_INDICATOR_CAPTURE, BM_SETCHECK, affinity == WDA_EXCLUDEFROMCAPTURE, 0);
                Set(IDC_SETTINGS_INDICATOR_ON_TOP, BM_SETCHECK, _cfg.onTopExclusive, 0);
                Set(IDC_SETTINGS_INDICATOR_COMBO, CB_RESETCONTENT, 0, 0);

                // Add strings to combobox
                for (const auto& state : IndicatorStates) {
                    Set(IDC_SETTINGS_INDICATOR_COMBO, CB_ADDSTRING, 0, (LPARAM)state);
                }
                Set(IDC_SETTINGS_INDICATOR_COMBO, CB_SETCURSEL, (WPARAM)_cfg.indicator, 0);

                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR),
                    1, MAKELONG(10, 32), _cfg.indicatorSize
                );
                Utils::InitTrackbar(
                    GetDlgItem(hWnd, IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR),
                    1, MAKELONG(0, 100), _cfg.volumeThreshold * 100
                );
                break;
            }


            case IDD_SETTINGS_SOUNDS:
                // Handle Sounds section activation
                printf("Sounds section activated\n");
                break;

            case IDD_SETTINGS_HOTKEYS:
                // Handle Hotkeys section activation
                printf("Hotkeys section activated\n");
                break;

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
                _cfg.excludeFromCapture = Utils::IsCheckboxCheck(hWnd, buttonId);
                break;
            case IDC_SETTINGS_INDICATOR_ON_TOP:
                _cfg.onTopExclusive = Utils::IsCheckboxCheck(hWnd, buttonId);
                break;
        }
    }

    void HandleComboBoxChange(HWND hWnd, int comboBoxId) {
        switch (comboBoxId) {
            case IDC_SETTINGS_INDICATOR_COMBO:
                _cfg.indicator = static_cast<IndicatorState>(SendMessage(GetDlgItem(hWnd, comboBoxId), CB_GETCURSEL, 0, 0));
                break;
        }
    }

    void HandleTrackbarChange(HWND hWnd, int trackbarId, int value) {
        switch (trackbarId) {
            case IDC_SETTINGS_INDICATOR_SIZE_TRACKBAR:
                _cfg.indicatorSize = static_cast<BYTE>(value);
                MainWindow->UpdateRect()
                    ->SetWidth(value)
                    ->SetHeight(value)
                    ->RefreshPos(nullptr)
                    ->Invalidate();
                break;
            case IDC_SETTINGS_INDICATOR_THRESHOLD_TRACKBAR:
                _cfg.volumeThreshold = static_cast<float>(value) / 100.0f;
                break;
        }
    }

    void Init() override {
        _cfgPrev = _cfg;
        MainWindow = _view->GetParent();

        MainWindow->Show();
        // Allow drag move on parent window by removing WS_EX_TRANSPARENT
        LONG_PTR dwExStyle = GetWindowLongPtr(MainWindow->GetEffectiveHandle(), GWL_EXSTYLE);
        dwExStyle &= ~WS_EX_TRANSPARENT; //Deleting WS_EX_TRANSPARENT
        SetWindowLongPtr(MainWindow->GetEffectiveHandle(), GWL_EXSTYLE, dwExStyle);

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
        _view->OnApply += [this]() {
            MainWindow->UpdateRect();
            _cfg.windowPosX = static_cast<USHORT>(MainWindow->GetPositionX());
            _cfg.windowPosY = static_cast<USHORT>(MainWindow->GetPositionY());

            if (_cfg != _cfgPrev) {
                AppConfig::Save(&_cfg, AppConfig::GetConfigPath());
                _cfgPrev = _cfg;
            }
        };
        _view->OnExit += [this]() {
            auto handle = MainWindow->GetEffectiveHandle();
            SetWindowLongPtr(handle, GWL_EXSTYLE,
            GetWindowLongPtr(handle, GWL_EXSTYLE) | WS_EX_TRANSPARENT);

            // Revert changes if needed
            _cfg = _cfgPrev;
        };
    }
};
#endif //EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
