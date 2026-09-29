#pragma once

#include <algorithm>
#include <windows.h>

#include "BaseWindow.hpp"
#include "LayeredWindow.hpp"
#include "UIAccess/UIAccess.hpp"

/// The overlay's window: the frame's own, or one made inside a UIAccess process to sit above everything.
class OverlayWindow {
public:
    static constexpr auto StyleEx = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    static constexpr auto Style = WS_POPUP | WS_DISABLED;

    POINT Pos{};
    POINT Size{};

    explicit OverlayWindow(const BaseWindow& frame) : _frame(frame) {}

    /// Timers and messages always go to the frame; only the drawing follows the target.
    HWND Frame() const { return _frame.GetHandle(); }
    HWND Target() const { return _onShadow ? _shadow : Frame(); }
    bool OnShadow() const { return _onShadow; }

    void EnterShadow() {
        if (!IsWindow(_shadow)) {
            _shadow = UIAccess::GetOrCreateWindow(ShadowWindowKey, StyleEx, Style);
        }
        _onShadow = _shadow != nullptr;
    }

    void LeaveShadow() {
        Hide();
        _onShadow = false;
    }

    void Show() {
        const HWND target = Target();
        if (!target || _visible) {
            return;
        }
        ShowWindow(target, SW_SHOW);
        _visible = true;
        Invalidate();
        UpdateWindow(target);
    }

    void Hide() {
        const HWND target = Target();
        if (!target || !_visible) {
            return;
        }
        ShowWindow(target, SW_HIDE);
        _visible = false;
    }

    void Invalidate() const {
        if (!_visible) {
            return;
        }
        if (_onShadow) {
            SendMessageW(Frame(), WM_PAINT, 0, 0);
            return;
        }
        InvalidateRect(Frame(), nullptr, TRUE);
    }

    /// Keeps the window inside the work area; the clamp is written back into Pos.
    void Place(HWND insertAfter) {
        const HWND target = Target();
        if (!target) {
            return;
        }

        MONITORINFO info{sizeof(MONITORINFO)};
        if (const HMONITOR monitor = MonitorFromPoint(Pos, MONITOR_DEFAULTTONEAREST);
            monitor && GetMonitorInfoW(monitor, &info)) {
            const RECT& work = info.rcWork;
            Pos.x = std::clamp(Pos.x, work.left, std::max(work.left, work.right - Size.x));
            Pos.y = std::clamp(Pos.y, work.top, std::max(work.top, work.bottom - Size.y));
        }

        SetWindowPos(target, insertAfter, Pos.x, Pos.y, Size.x, Size.y, SWP_NOACTIVATE);
    }

    void SyncBounds() {
        RECT rect;
        if (const HWND target = Target(); target && GetWindowRect(target, &rect)) {
            Pos = {rect.left, rect.top};
            Size = {rect.right - rect.left, rect.bottom - rect.top};
        }
    }

    void SetInteractive(bool interactive) const {
        const HWND target = Target();
        LONG_PTR exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
        LONG_PTR style = GetWindowLongPtrW(target, GWL_STYLE);
        exStyle = interactive ? exStyle & ~WS_EX_TRANSPARENT : exStyle | WS_EX_TRANSPARENT;
        style = interactive ? style & ~WS_DISABLED : style | WS_DISABLED;
        SetWindowLongPtrW(target, GWL_EXSTYLE, exStyle);
        SetWindowLongPtrW(target, GWL_STYLE, style);
        SetWindowPos(target, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void SetDisplayAffinity(DWORD affinity) const {
        DWORD current = 0;
        GetWindowDisplayAffinity(Target(), &current);
        if (current == affinity) {
            return;
        }
        if (_onShadow) {
            UIAccess::InjectDisplayAffinity(Target(), affinity);
        } else {
            SetWindowDisplayAffinity(Frame(), affinity);
        }
    }

    void Paint(const LayeredWindow::RenderCallback& render) {
        LayeredWindow::Render(Target(), _canvas, Size.x, Size.y, Pos, render);
    }

private:
    /// Looked up by title from another process: renaming it orphans the window a running instance made.
    static constexpr const char* ShadowWindowKey = "EasyLauncherIndicator";

    const BaseWindow& _frame;
    LayeredWindow::Surface _canvas;
    HWND _shadow = nullptr;
    bool _onShadow = false;
    bool _visible = false;
};
