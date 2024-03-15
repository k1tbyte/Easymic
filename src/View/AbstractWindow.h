#ifndef EASYMIC_ABSTRACTWINDOW_H
#define EASYMIC_ABSTRACTWINDOW_H

#include <Windows.h>
#include <unordered_map>
#include <functional>

typedef std::function<void(WPARAM, LPARAM)> WindowEvent;

class AbstractWindow {

protected:
    HINSTANCE hInst = nullptr;
    HWND hWnd = nullptr;

    const std::unordered_map<UINT, WindowEvent>* callbackList;

    AbstractWindow(HINSTANCE hInstance, const std::unordered_map<UINT, WindowEvent>* events)
    {
        callbackList = events;
        this->hInst = hInstance;
    }

    LRESULT CALLBACK WindowHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if(message == WM_NCHITTEST) {
            LRESULT position = DefWindowProc(hwnd, message, wParam, lParam);
            return position == HTCLIENT ? HTCAPTION : position;
        }

        if(auto it {callbackList->find(message)};
                it != std::end(*callbackList))
        {
            hWnd = hwnd;
            (it->second)(wParam,lParam);
            return 0;
        }
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

public:
    virtual void Show() {
        ShowWindow(hWnd,SW_SHOW);
    }

    virtual void Hide() {
        ShowWindow(hWnd,SW_HIDE);
    }

    virtual void Close() {
        DestroyWindow(hWnd);
    }

};

#endif //EASYMIC_ABSTRACTWINDOW_H
