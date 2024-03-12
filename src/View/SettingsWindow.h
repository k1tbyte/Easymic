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

public:

    SettingsWindow(HINSTANCE hInstance, HWND* ownerHwnd) : AbstractWindow(hInstance, &events)
    {
        settings = this;
        this->ownerHwnd = ownerHwnd;
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
        AbstractWindow::Show();
    }

private:

    void OnInit(WPARAM wParam, LPARAM lParam) {
        Utils::CenterWindowOnScreen(hWnd);
    }

    void OnDestroy(WPARAM wParam, LPARAM lParam) {
        SetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE, GetWindowLongPtr(*ownerHwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        hWnd = nullptr;
    }

    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_INITDIALOG, [this](WPARAM wParam, LPARAM lParam) { OnInit(wParam, lParam); }  },
            {WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) { OnDestroy(wParam, lParam); }  },
    };
};


#endif //EASYMIC_SETTINGSWINDOW_H

