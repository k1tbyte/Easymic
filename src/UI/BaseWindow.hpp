#pragma once

#include <windows.h>

class BaseWindow {
public:
    virtual ~BaseWindow() {
        // The window can outlive us on the way out of WinMain: its proc must not reach freed memory
        if (_hwnd) {
            SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
        }
    }

    BaseWindow(const BaseWindow&) = delete;
    BaseWindow& operator=(const BaseWindow&) = delete;

    static BaseWindow* FromHandle(HWND hwnd) {
        return hwnd ? reinterpret_cast<BaseWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) : nullptr;
    }

    HWND GetHandle() const { return _hwnd; }

    void Close() {
        if (_hwnd) {
            DestroyWindow(_hwnd);
        }
    }

protected:
    explicit BaseWindow(HINSTANCE hInstance) : _hInstance(hInstance) {}

    virtual LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(_hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        BaseWindow* window = FromHandle(hwnd);

        if (!window) {
            if (message != WM_CREATE) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            window = static_cast<BaseWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            window->RegisterWindow(hwnd);
        } else if (message == WM_DESTROY) {
            window->_hwnd = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }

        return window->HandleMessage(message, wParam, lParam);
    }

    void RegisterWindow(HWND hwnd) {
        _hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }

    HWND _hwnd = nullptr;
    HINSTANCE _hInstance = nullptr;
};
