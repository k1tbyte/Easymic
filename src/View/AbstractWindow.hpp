#ifndef EASYMIC_ABSTRACTWINDOW_HPP
#define EASYMIC_ABSTRACTWINDOW_HPP

#include <unordered_map>
#include <functional>

typedef std::function<void(WPARAM, LPARAM)> WindowEvent;

class AbstractWindow {

protected:
    HINSTANCE hInst = nullptr;
    HWND hWnd = nullptr;
    bool isShown = false;

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
    virtual ~AbstractWindow() = default;

    inline HWND GetHwnd() { return hWnd; }

    [[nodiscard]] inline bool IsShown() const { return isShown; }

    virtual void Show() {
        if(isShown) {
            return;
        }

        ShowWindow(hWnd,SW_SHOW);
        InvalidateRect(hWnd, nullptr, TRUE);
        isShown = true;
    }

    virtual void Hide() {
        if(!isShown) {
            return;
        }

        ShowWindow(hWnd,SW_HIDE);
        isShown = false;
    }

    inline virtual void Show(int showCode) {
        if(showCode == SW_SHOW) {
            Show();
            return;
        }

        Hide();
    }

    inline virtual void Close() {
        DestroyWindow(hWnd);
    }

};

#endif //EASYMIC_ABSTRACTWINDOW_HPP
