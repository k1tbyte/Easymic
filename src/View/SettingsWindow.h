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
    Config* config;
    Config configTemp;

public:

    SettingsWindow(HINSTANCE hInstance, HWND* ownerHwnd,Config* config) : AbstractWindow(hInstance, &events)
    {
        settings = this;
        this->config = config;
        this->ownerHwnd = ownerHwnd;
    }

    void InitControls() {

        if(Utils::IsInAutoStartup(AppName)) {
            SendMessage(GetDlgItem(hWnd,ID_AUTOSTARTUP), BM_SETCHECK, BST_CHECKED, 0);
        }
        SetBindingState(config->muteHotkey);
    }

    inline void SetBindingState(DWORD hotkey) {
        SendMessage(
                GetDlgItem(this->hWnd, MUTE_HOTKEY),
                WM_SETTEXT, 0, (LPARAM) ((LPARAM) hotkey == 0 ? "Click to bind" :
                                         HotkeyManager::GetSequenceName(hotkey).c_str())
        );
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
                    *config = configTemp;
                    Config::Save(&configTemp, Config::GetConfigPath());
                }
            case IDCANCEL:
                this->Close();
                break;
            case MUTE_HOTKEY:
                HotkeyManager::Bind([this](DWORD mask)
                {
                    SetBindingState(mask);
                    this->configTemp.muteHotkey = mask;
                });
                break;

            case ID_AUTOSTARTUP: {
                lParam = Utils::IsInAutoStartup(AppName) ? Utils::RemoveFromAutoStartup(AppName) :
                         Utils::AddToAutoStartup(AppName);
                break;
            }
        }
    }


    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_INITDIALOG, [this](WPARAM wParam, LPARAM lParam) { OnInit(wParam, lParam); }  },
            {WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) { OnDestroy(wParam, lParam); }  },
            {WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) { OnCommand(wParam, lParam); }  }
    };
};


#endif //EASYMIC_SETTINGSWINDOW_H

