#ifndef EASYMIC_SETTINGSWINDOW_H
#define EASYMIC_SETTINGSWINDOW_H
#include <Windows.h>
#include <cstdio>
#include "../Resources/Resource.h"
#include "AbstractWindow.h"
#include "../Utils.hpp"

class SettingsWindow;

static SettingsWindow* settings;

class SettingsWindow final : AbstractWindow {

    HWND* ownerHwnd = nullptr;
    AudioManager* audioManager;
    Config* config;
    Config configTemp;
    HWND micVolTrackbar;
    HWND bellVolTrackbar;
    HWND muteHotkey;

public:

    SettingsWindow(HINSTANCE hInstance, HWND* ownerHwnd,Config* config, AudioManager* audioManager) : AbstractWindow(hInstance, &events)
    {
        settings = this;
        this->audioManager = audioManager;
        this->config = config;
        this->ownerHwnd = ownerHwnd;
    }

    void InitControls() {

        if(Utils::IsInAutoStartup(AppName)) {
            SendMessage(GetDlgItem(hWnd,ID_AUTOSTARTUP), BM_SETCHECK, BST_CHECKED, 0);
        }

        if(config->muteZeroMode) {
            SendMessage(GetDlgItem(hWnd,MUTE_MODE), BM_SETCHECK, BST_CHECKED, 0);
        }

        SetWindowTextW(GetDlgItem(hWnd, SOUNDS_GROUPBOX),
                       std::wstring(L"Sounds - ").append(audioManager->GetDefaultMicName())
                               .c_str());

        Utils::InitTrackbar(GetDlgItem(hWnd, BELL_VOLUME_TRACKBAR),1,MAKELONG(0,100),config->bellVolume);
        Utils::InitTrackbar(GetDlgItem(hWnd, MIC_VOLUME_TRACKBAR),1,MAKELONG(0,100), config->micVolume);
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
        muteHotkey = GetDlgItem(hWnd, MUTE_HOTKEY);
        configTemp = *config;
        HotkeyManager::UnregisterHotkey();
        InitControls();
        AbstractWindow::Show();
    }

private:

    void OnInit(WPARAM wParam, LPARAM lParam) {
        Utils::CenterWindowOnScreen(hWnd);
    }

    void OnDestroy(WPARAM wParam, LPARAM lParam) {
        HotkeyManager::RegisterHotkey(config->muteHotkey);
        SetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE, GetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        hWnd = nullptr;
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
    }


    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_INITDIALOG, [this](WPARAM wParam, LPARAM lParam) { OnInit(wParam, lParam); }  },
            {WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) { OnDestroy(wParam, lParam); }  },
            {WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) { OnCommand(wParam, lParam); }  },
            {WM_HSCROLL,[this](WPARAM wParam, LPARAM lParam) { OnHorizontalScroll(wParam, lParam); } }
    };
};


#endif //EASYMIC_SETTINGSWINDOW_H

