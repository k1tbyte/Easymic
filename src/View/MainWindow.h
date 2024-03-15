#ifndef EASYMIC_MAINWINDOW_H
#define EASYMIC_MAINWINDOW_H

#include <windows.h>
#include <unordered_map>
#include <cstdio>
#include "../Resources/Resource.h"
#include "../config.h"
#include "../AudioManager.hpp"
#include "../HotkeyManager.h"
#include "SettingsWindow.h"


class MainWindow final : public AbstractWindow {
    LPCWSTR name;
    AudioManager* audioManager;
    Config* config;
    SettingsWindow* settings;

    NOTIFYICONDATAW trayIcon{};
    int windowSize{};

    HICON appIcon{};
    HICON mutedIcon{};
    HICON unmutedIcon{};

    Resource muteSound;
    Resource unmuteSound;

private:

    void OnCreate(WPARAM wParam, LPARAM lParam);
    void OnDestroy(WPARAM wParam, LPARAM lParam);
    void OnPaint(WPARAM wParam, LPARAM lParam);
    void OnClose(WPARAM wParam, LPARAM lParam);
    void OnShellIcon(WPARAM wParam, LPARAM lParam);
    void OnCommand(WPARAM wParam, LPARAM lParam);

    void OnHotkeyPressed();

    void SwitchMicState(bool playSound = true);
    void SetMuteMode(BOOL muted);

    HRGN _getWindowRegion() const;
    void _initComponents();

public:
    MainWindow(LPCWSTR name, HINSTANCE hInstance, Config* config, AudioManager* audioManager);
    bool InitWindow();
    bool InitTrayIcon();

    void SetWindowSize(int width, int height) {
        RECT rcClient;//screen size

        GetClientRect(hWnd, &rcClient);
        SetWindowPos(hWnd,nullptr, 20,20 ,
                     width, height,SWP_NOSIZE | SWP_NOZORDER);
    }


private:

    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_CREATE, [this](WPARAM wParam, LPARAM lParam) { OnCreate(wParam, lParam); }  },
            {WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) { OnDestroy(wParam, lParam); }  },
            {WM_PAINT, [this](WPARAM wParam, LPARAM lParam) { OnPaint(wParam, lParam); }  },
            {WM_CLOSE, [this](WPARAM wParam, LPARAM lParam) { OnClose(wParam, lParam); }  },
            {WM_SHELLICON, [this](WPARAM wParam, LPARAM lParam) { OnShellIcon(wParam, lParam); }  },
            {WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) { OnCommand(wParam, lParam); }  },
            {WM_UPDATE_MIC, [this](WPARAM wParam, LPARAM lParam) { _initComponents(); SwitchMicState(); }  }
    };
};


#endif //EASYMIC_MAINWINDOW_H
