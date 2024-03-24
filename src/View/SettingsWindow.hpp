#ifndef EASYMIC_SETTINGSWINDOW_HPP
#define EASYMIC_SETTINGSWINDOW_HPP
#include <Windows.h>
#include <cstdio>
#include "../Resources/Resource.h"
#include "AbstractWindow.hpp"
#include "../Utils.hpp"

class SettingsWindow;

static SettingsWindow* settings;

class SettingsWindow final : public AbstractWindow {

    constexpr static const char* IndicatorStates[] = {
            "Hidden", "Muted", "Muted or talking", "Always", "Always and talking"
    };

    HWND* ownerHwnd = nullptr;
    HICON appIcon = nullptr;
    AudioManager* audioManager = nullptr;
    Config* config = nullptr;
    Config configTemp;
    HWND micVolTrackbar = nullptr;
    HWND bellVolTrackbar = nullptr;
    HWND thresholdTrackbar = nullptr;
    HWND muteHotkey = nullptr;
    HWND indicatorCombo = nullptr;
    HWND indicatorSizeCombo = nullptr;
    std::function<void()> reinitializeAction;

public:

    SettingsWindow(HINSTANCE hInstance, HWND* ownerHwnd,Config* config, AudioManager* audioManager, HICON icon,
                   const std::function<void()>& reinitializeCallback) : AbstractWindow(hInstance, &events)
    {
        settings = this;
        appIcon = icon;
        this->audioManager = audioManager;
        this->config = config;
        this->ownerHwnd = ownerHwnd;
        reinitializeAction = reinitializeCallback;
    }

    Config* GetTempSettings() { return &configTemp; }

    void InitControls() {
        SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);

        if(Utils::IsInAutoStartup(AppName)) {
            SendMessage(GetDlgItem(hWnd,ID_AUTOSTARTUP), BM_SETCHECK, BST_CHECKED, 0);
        }

        if(config->muteZeroMode) {
            SendMessage(GetDlgItem(hWnd,MUTE_MODE), BM_SETCHECK, BST_CHECKED, 0);
        }

        SetWindowTextW(GetDlgItem(hWnd, SOUNDS_GROUPBOX),
                       std::wstring(L"Sounds - ").append(audioManager->GetDefaultMicName())
                               .c_str());

        for (const auto& IndicatorState : IndicatorStates) {
            SendMessage(indicatorCombo,(UINT) CB_ADDSTRING,(WPARAM) 0,(LPARAM) IndicatorState);
        }

        SendMessage(indicatorSizeCombo,(UINT) CB_ADDSTRING,(WPARAM) 0,(LPARAM) "Tiny");
        SendMessage(indicatorSizeCombo,(UINT) CB_ADDSTRING,(WPARAM) 0,(LPARAM) "Large");
        SendMessage(indicatorSizeCombo, CB_SETCURSEL, (WPARAM)
            (config->indicatorSize == (BYTE)IndicatorSize::Large ? 1 :0), (LPARAM)0);

        SendMessage(indicatorCombo, CB_SETCURSEL, (WPARAM)config->indicator, (LPARAM)0);


        Utils::InitTrackbar(bellVolTrackbar,1,MAKELONG(0,100),config->bellVolume);
        Utils::InitTrackbar(micVolTrackbar,1,MAKELONG(0,100), config->micVolume);
        Utils::InitTrackbar(thresholdTrackbar,1,MAKELONG(0,100),
                            (int)(config->volumeThreshold*100));

        if(config->indicator == IndicatorState::MutedOrTalk || config->indicator == IndicatorState::AlwaysAndTalk) {
            EnableWindow(thresholdTrackbar,true);
        }
        SetBindingState(config->muteHotkey);
    }

    void ApplyChanges() {
        if(config->bellVolume != configTemp.bellVolume) {
            audioManager->SetAppVolume(configTemp.bellVolume);
        }
        if(config->muteZeroMode != configTemp.muteZeroMode) {
            config->muteZeroMode = configTemp.muteZeroMode;
            SendMessage(*ownerHwnd,WM_UPDATE_MIC,0,0);
        }
        if(config->micVolume != configTemp.micVolume && (!configTemp.muteZeroMode || !CurrentlyMuted())) {
            audioManager->SetMicVolume(configTemp.micVolume);
        }
    }

    inline void SetBindingState(DWORD hotkey) {
        SendMessage(muteHotkey,WM_SETTEXT, 0,
                    (LPARAM) (hotkey == 0 ? "Click to bind" :
                            HotkeyManager::GetSequenceName(hotkey).c_str()));
    }

    bool CurrentlyMuted() {
        bool isMuteState = audioManager->IsMicMuted();

        if (!config->muteZeroMode) {
            return isMuteState;
        }

        // If the mode is muted, need to turn on for mute/unmute by volume
        if(isMuteState) {
            audioManager->SetMicState(false);
        }

        return audioManager->GetMicVolume() == 0;
    }

    void Show() override {
        if(hWnd) {
            SetForegroundWindow(hWnd);
            return;
        }

        // Allows adjust the position of the indicator window
        LONG_PTR dwExStyle = GetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE);
        dwExStyle &= ~WS_EX_TRANSPARENT; //Deleting WS_EX_TRANSPARENT
        SetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE, dwExStyle);

        hWnd = CreateDialog(hInst, MAKEINTRESOURCE(IDD_SETTINGS), *ownerHwnd,
                            [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
                            { return settings->WindowHandler(hwnd,msg,wp,lp); }
        );

        micVolTrackbar = GetDlgItem(hWnd,MIC_VOLUME_TRACKBAR);
        bellVolTrackbar = GetDlgItem(hWnd,BELL_VOLUME_TRACKBAR);
        thresholdTrackbar = GetDlgItem(hWnd,THRESHOLD_TRACKBAR);
        muteHotkey = GetDlgItem(hWnd, MUTE_HOTKEY);
        indicatorCombo = GetDlgItem(hWnd,INDICATOR_COMBO);
        indicatorSizeCombo = GetDlgItem(hWnd,INDICATOR_SIZE_COMBO);
        configTemp = *config;
        InitControls();
        AbstractWindow::Show();
    }

private:

    void OnInit(WPARAM wParam, LPARAM lParam) {
        Utils::CenterWindowOnScreen(hWnd);
    }

    void OnDestroy(WPARAM wParam, LPARAM lParam) {
        HotkeyManager::RegisterHotkey(config->muteHotkey);
        // Lock indicator position
        SetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE, GetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        hWnd = nullptr;
        isShown = false;
        reinitializeAction();
    }

    void OnCommand(WPARAM wParam, LPARAM lParam) {
        switch(LOWORD(wParam))
        {
            case IDOK:
                if(*config != configTemp) {
                    ApplyChanges();
                    *config = configTemp;
                    Config::Save(&configTemp, Config::GetConfigPath());
                }
            case IDCANCEL:
                this->Close();
                break;
            case MUTE_HOTKEY:
                SendMessage(muteHotkey,WM_SETTEXT, 0, (LPARAM)"Waiting for the hotkey...");
                HotkeyManager::Bind([this](DWORD mask)
                {
                    SetBindingState(mask);
                    this->configTemp.muteHotkey = mask;
                });
                break;
            case ID_AUTOSTARTUP:
                lParam = Utils::IsInAutoStartup(AppName) ? Utils::RemoveFromAutoStartup(AppName) :
                         Utils::AddToAutoStartup(AppName);
                break;
            case MUTE_MODE:
                configTemp.muteZeroMode = !configTemp.muteZeroMode;
                break;
            case INDICATOR_COMBO:
                if(HIWORD(wParam) == CBN_SELCHANGE) {
                    config->indicator = configTemp.indicator = (IndicatorState)SendMessage(indicatorCombo, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
                    SendMessage(*ownerHwnd,WM_UPDATE_STATE,0,0);
                    EnableWindow(thresholdTrackbar,configTemp.indicator == IndicatorState::AlwaysAndTalk ||
                            configTemp.indicator == IndicatorState::MutedOrTalk);
                }
                break;

            case INDICATOR_SIZE_COMBO:
                if(HIWORD(wParam) == CBN_SELCHANGE) {
                    configTemp.indicatorSize = (BYTE)(SendMessage(indicatorCombo, CB_GETCURSEL, (WPARAM)0, (LPARAM)0) ?
                            IndicatorSize::Tiny : IndicatorSize::Large);
                }
                break;
        }
    }

    void OnHorizontalScroll(WPARAM wParam, LPARAM lParam) {
        if(LOWORD(wParam) != TB_ENDTRACK) {
            return;
        }

        HWND trackHwnd = (HWND)lParam;
        if(trackHwnd == bellVolTrackbar) {
            configTemp.bellVolume = SendMessage(bellVolTrackbar, TBM_GETPOS, 0, 0);
        }
        else if(trackHwnd == micVolTrackbar) {
            configTemp.micVolume = SendMessage(micVolTrackbar, TBM_GETPOS, 0, 0);
        }
        else if(trackHwnd == thresholdTrackbar) {
            configTemp.volumeThreshold = (float)SendMessage(thresholdTrackbar, TBM_GETPOS, 0, 0) / 100;
        }
    }

    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_INITDIALOG, [this](WPARAM wParam, LPARAM lParam) { OnInit(wParam, lParam); }  },
            {WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) { OnDestroy(wParam, lParam); }  },
            {WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) { OnCommand(wParam, lParam); }  },
            {WM_HSCROLL,[this](WPARAM wParam, LPARAM lParam) { OnHorizontalScroll(wParam, lParam); } }
    };
};


#endif //EASYMIC_SETTINGSWINDOW_HPP

