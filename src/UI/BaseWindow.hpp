#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <windows.h>

#include "definitions.h"
#include "RateLimiter.hpp"
#include "ViewModel.hpp"

template<typename T>
concept IViewModelType = std::is_base_of_v<IViewModel, T>;

/**
 * @brief Base class for all application windows.
 *
 * Owns its view model; the view model points back with a raw pointer, so the pair is destroyed
 * with the window. Message dispatch is a virtual override in the derived window - a table of
 * std::function would be consulted on every message the desktop sends.
 */
class BaseWindow {
public:
    virtual ~BaseWindow() {
        // The window can outlive us on the way out of WinMain - detach it, or its proc would
        // route the next message into freed memory
        if (_hwnd) {
            SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
        }
    }

    /// The window behind a handle, or null before it has been claimed. Replaces a registry:
    /// the slot lives with the window, so there is nothing to look up, lock or destroy in order.
    static BaseWindow* FromHandle(HWND hwnd) {
        return hwnd ? reinterpret_cast<BaseWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) : nullptr;
    }

    BaseWindow(const BaseWindow&) = delete;
    BaseWindow& operator=(const BaseWindow&) = delete;

    HWND GetHandle() const { return _hwnd; }
    HINSTANCE GetHInstance() const { return _hInstance; }
    bool IsVisible() const { return _isVisible; }
    BaseWindow* GetParent() const { return _parent; }

    HWND GetEffectiveHandle() const { return _shadowHwnd ? _shadowHwnd : _hwnd; }
    bool IsOvershadowed() const { return _shadowHwnd != nullptr; }
    void SetShadowHwnd(HWND hwnd) { _shadowHwnd = hwnd; }

    virtual void Invalidate() {
        if (!_isVisible) {
            return;
        }

        if (_shadowHwnd) {
            SendMessage(_hwnd, WM_PAINT, 0, 0);
            return;
        }

        InvalidateRect(_hwnd, nullptr, TRUE);
    }

    virtual void Show() { _show(GetEffectiveHandle()); }
    virtual void Hide() { _hide(GetEffectiveHandle()); }
    virtual void Close() { _close(GetEffectiveHandle()); }

    BaseWindow* SetPositionX(LONG x) { _pos.x = x; return this; }
    BaseWindow* SetPositionY(LONG y) { _pos.y = y; return this; }
    BaseWindow* SetWidth(LONG width) { _size.x = width; return this; }
    BaseWindow* SetHeight(LONG height) { _size.y = height; return this; }

    LONG GetPositionX() const { return _pos.x; }
    LONG GetPositionY() const { return _pos.y; }
    LONG GetWidth() const { return _size.x; }
    LONG GetHeight() const { return _size.y; }

    virtual BaseWindow* UpdateRect() { return _updateRect(GetEffectiveHandle()); }
    virtual BaseWindow* RefreshPos(HWND insertAfter) { return _refreshPos(GetEffectiveHandle(), insertAfter); }

    template <IViewModelType T, typename... Args>
    T* AttachViewModel(Args &&... args) {
        auto viewModel = std::make_unique<T>(this, std::forward<Args>(args)...);
        T* attached = viewModel.get();
        _viewModel = std::move(viewModel);
        return attached;
    }

protected:
    explicit BaseWindow(HINSTANCE hInstance) : _hInstance(hInstance) {}

    /// Derived windows override this and switch on the message they care about.
    virtual LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        // Special handling for drag caption
        if (message == WM_NCHITTEST) {
            const LRESULT result = DefWindowProcW(_hwnd, message, wParam, lParam);
            return result == HTCLIENT ? HTCAPTION : result;
        }

#if _DEBUG
        if (message == WM_PAINT) {
            MEASURE_RATE(RenderDebugLimiter, 50, 1000, {
                Beep(1600, 150);
                throw std::runtime_error("Anomaly detected in WM_PAINT frequency");
            });
        }
#endif

        return DefWindowProcW(_hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        BaseWindow* window = FromHandle(hwnd);

        if (!window) {
            if ((message != WM_INITDIALOG && message != WM_CREATE) || lParam == 0) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }

            window = message == WM_INITDIALOG
                         ? reinterpret_cast<BaseWindow*>(lParam)
                         : static_cast<BaseWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            window->RegisterWindow(hwnd);

            RECT rect;
            GetWindowRect(hwnd, &rect);
            window->SetHeight(rect.bottom - rect.top)
                  ->SetWidth(rect.right - rect.left)
                  ->SetPositionX(rect.left)
                  ->SetPositionY(rect.top);
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

    BaseWindow* _updateRect(HWND hWnd) {
        if (!hWnd) {
            return this;
        }

        RECT rect;
        GetWindowRect(hWnd, &rect);
        _size.x = rect.right - rect.left;
        _size.y = rect.bottom - rect.top;
        _pos.x = rect.left;
        _pos.y = rect.top;
        return this;
    }

    BaseWindow* _refreshPos(HWND hWnd, HWND insertAfter) {
        if (!hWnd) {
            return this;
        }

        const POINT anchor{_pos.x, _pos.y};
        if (const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST)) {
            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(monitor, &monitorInfo)) {
                const auto &workArea = monitorInfo.rcWork;
                const LONG maxX = std::max(workArea.left, workArea.right - _size.x);
                const LONG maxY = std::max(workArea.top, workArea.bottom - _size.y);
                _pos.x = std::clamp(_pos.x, workArea.left, maxX);
                _pos.y = std::clamp(_pos.y, workArea.top, maxY);
            }
        }

        SetWindowPos(hWnd, insertAfter, _pos.x, _pos.y, _size.x, _size.y,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
        return this;
    }

    void _close(HWND hWnd) const {
        if (hWnd) {
            DestroyWindow(hWnd);
        }
    }

    void _hide(HWND hWnd) {
        if (!hWnd || !_isVisible) {
            return;
        }
        ShowWindow(hWnd, SW_HIDE);
        _isVisible = false;
    }

    void _show(HWND hWnd) {
        if (!hWnd || _isVisible) {
            return;
        }

        ShowWindow(hWnd, SW_SHOW);
        _isVisible = true;
        Invalidate();
        UpdateWindow(hWnd);
    }

    POINT _size{};
    POINT _pos{};
    HWND _hwnd = nullptr;
    HWND _shadowHwnd = nullptr;
    BaseWindow* _parent = nullptr;
    std::unique_ptr<IViewModel> _viewModel;
    HINSTANCE _hInstance = nullptr;
    bool _isVisible = false;
};

