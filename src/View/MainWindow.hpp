#ifndef EASYMIC_MAINWINDOW_HPP
#define EASYMIC_MAINWINDOW_HPP

#include <unordered_map>
#include <cstdio>
#include "../config.hpp"
#include "../Audio/AudioManager.hpp"
#include "../Lib/HotkeyManager.hpp"
#include "SettingsWindow.hpp"

#define WND_PADDING 2

class MainWindow final : public AbstractWindow {
    LPCWSTR name;
    AudioManager& audioManager;
    Config& config;
    SettingsWindow* settings;

    NOTIFYICONDATAW trayIcon{};
    int windowSize{};
    bool peakMeterActive{};
    bool micMuted = false;

    // if the mic captures sound
    BYTE micActive{};
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

    void SwitchMicState();
    void Reinitialize();
    void _initComponents();

public:
    MainWindow(LPCWSTR name, HINSTANCE hInstance, Config& config, AudioManager& audioManager);
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
        static const auto& state = config.indicator;

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

    void SetMicState(bool playSound) {
        Resource* sound;
        if(this->micMuted) {
            trayIcon.hIcon = mutedIcon;
            sound = &muteSound;
        }
        else {
            trayIcon.hIcon = unmutedIcon;
            sound = &unmuteSound;
        }

        if(playSound) {
            PlaySound((LPCSTR) sound->buffer,hInst,SND_ASYNC | SND_MEMORY);
        }

        if(config.indicator != IndicatorState::Hidden) {
            UpdateWindowState();
            InvalidateRect(this->hWnd, nullptr, TRUE);
        }

        Shell_NotifyIconW(NIM_MODIFY, &trayIcon);
    }


    int timerCallbacks = 0;

    void OnTimer(WPARAM wParam, LPARAM lParam) {
        if(wParam != MIC_PEAK_TIMER) {
            return;
        }

        const auto& peak = audioManager.CaptureDevice()->GetPeak();
        if(peak > config.volumeThreshold) {
            if(!micActive) {
#ifdef DEBUG_AUDIO
                printf("The microphone has become active\n");
#endif
                micActive = 2;
                UpdateWindowState();
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        else if(micActive) {
            // This is necessary so that the indicator does not irritate with its frequent blinking
            // There will be a slight delay after the volume indicator goes silent.
            micActive--;
            if(!micActive) {
#ifdef DEBUG_AUDIO
                printf("The microphone has switched to a passive state\n");
#endif
                UpdateWindowState();
            }
        }
    }

    void UpdateWindowSize() {
        RECT rect;
        GetWindowRect(hWnd, &rect);
        windowSize = config.indicatorSize * WND_PADDING;
        SetWindowPos(hWnd, nullptr, rect.left, rect.top, windowSize, windowSize, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hWnd, nullptr, TRUE);
    }


    void OnSessionUpdate() {
        const auto count = audioManager.CaptureDevice()->GetActiveSessionsCount();

        // !!! Important !!!
        // If we directly call this->Hide this->Show, strange things will
        // happen to the window (as with all the crap in WinApi)
        // Perhaps this has something to do with the fact that the calls are coming from different threads, idk
        if(count > 0) {

            if(!peakMeterActive && (config.indicator == IndicatorState::AlwaysAndTalk ||
                                    config.indicator == IndicatorState::MutedOrTalk)) {
                SetTimer(hWnd,MIC_PEAK_TIMER,150,nullptr);
                peakMeterActive = true;
                                    }

            hasActiveSessions = true;
            SendMessage(hWnd,WM_UPDATE_STATE, 0, 0);
            return;
        }

        if(peakMeterActive) {
            KillTimer(hWnd,MIC_PEAK_TIMER);
            peakMeterActive = false;
        }

        hasActiveSessions = false;
        SendMessage(hWnd,WM_UPDATE_STATE, 0, 0);
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
            {WM_USER + 5, [this](WPARAM wParam, LPARAM lParam) { UpdateWindowSize(); }  },
            {WM_UPDATE_MIC, [this](WPARAM wParam, LPARAM lParam) { SetMicState(lParam); }  },
            {WM_UPDATE_STATE, [this](WPARAM wParam, LPARAM lParam){ UpdateWindowState(); } },
            // We need to re-initialize the tray icon when the explorer restarts or crashes.
            { RegisterWindowMessageA("TaskbarCreated"), [this](WPARAM wParam, LPARAM lParam){ InitTrayIcon(); } }
    };
};


#endif //EASYMIC_MAINWINDOW_HPP
