#ifndef EASYMIC_MAINWINDOW_HPP
#define EASYMIC_MAINWINDOW_HPP

#include <windows.h>
#include <unordered_map>
#include <cstdio>
#include "../Resources/Resource.h"
#include "../config.hpp"
#include "../Audio/AudioManager.hpp"
#include "../HotkeyManager.hpp"
#include "SettingsWindow.hpp"


class MainWindow final : public AbstractWindow {
    LPCWSTR name;
    AudioManager* audioManager;
    Config* config;
    SettingsWindow* settings;

    NOTIFYICONDATAW trayIcon{};
    int windowSize{};
    bool peakMeterActive{};
    bool micMuted{};

    // if the mic captures sound
    bool micActive{};
    // if at least one microphone session is active
    bool hasActiveSessions{};

    HICON appIcon{};
    HICON mutedIcon{};
    HICON unmutedIcon{};
    HICON activeIcon{};

    Resource muteSound;
    Resource unmuteSound;

private:

    void OnCreate(WPARAM wParam, LPARAM lParam);
    void OnDestroy(WPARAM wParam, LPARAM lParam);
    void OnPaint(WPARAM wParam, LPARAM lParam);
    void OnClose(WPARAM wParam, LPARAM lParam);
    void OnShellIcon(WPARAM wParam, LPARAM lParam);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnDragEnd(WPARAM wParam, LPARAM lParam);

    void OnHotkeyPressed();

    void SwitchMicState(bool playSound = true);
    void SetMuteMode(BOOL muted);
    void Reinitialize();
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

    void UpdateWindowState() {
        using enum IndicatorState;
        static const auto& state = config->indicator;

        if(!hasActiveSessions || state == Hidden) {
            this->Hide();
            return;
        }

        if(state == Always || state == AlwaysAndTalk || micMuted || (micActive && state == MutedOrTalk)) {
            this->Show();
            return;
        }
        this->Hide();

    }


    void OnTimer(WPARAM wParam, LPARAM lParam) {
        if(wParam != MIC_PEAK_TIMER) {
            return;
        }

        const auto& peak = audioManager->GetMicPeak();
        if(peak > config->volumeThreshold) {
            if(!micActive) {
#ifdef DEBUG_AUDIO
                printf("The microphone has become active\n");
#endif
                micActive = true;
                UpdateWindowState();
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        else if(micActive) {
#ifdef DEBUG_AUDIO
            printf("The microphone has switched to a passive state\n");
#endif
            micActive = false;
            UpdateWindowState();
            InvalidateRect(hWnd,nullptr, TRUE);
        }
    }

private:

    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_CREATE, [this](WPARAM wParam, LPARAM lParam) { OnCreate(wParam, lParam); }  },
            {WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) { OnDestroy(wParam, lParam); }  },
            {WM_PAINT, [this](WPARAM wParam, LPARAM lParam) { OnPaint(wParam, lParam); }  },
            {WM_CLOSE, [this](WPARAM wParam, LPARAM lParam) { OnClose(wParam, lParam); }  },
            {WM_SHELLICON, [this](WPARAM wParam, LPARAM lParam) { OnShellIcon(wParam, lParam); }  },
            {WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) { OnCommand(wParam, lParam); }  },
            {WM_EXITSIZEMOVE, [this](WPARAM wParam, LPARAM lParam) { OnDragEnd(wParam, lParam); }  },
            {WM_TIMER, [this](WPARAM wParam, LPARAM lParam) { OnTimer(wParam, lParam); }  },
            {WM_UPDATE_MIC, [this](WPARAM wParam, LPARAM lParam) { _initComponents(); SwitchMicState(); }  },
            {WM_UPDATE_STATE, [this](WPARAM wParam, LPARAM lParam){ UpdateWindowState(); } }
    };
};


#endif //EASYMIC_MAINWINDOW_HPP
