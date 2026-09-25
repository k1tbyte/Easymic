#include "WindowList.hpp"

#include "AppConfig.hpp"
#include "Foreground.hpp"
#include "Str.hpp"
#include "WindowCatalog.hpp"

#include <dwmapi.h>

namespace {

    /// rcNormalPosition is in workspace coordinates: shifted by the taskbar of the rect's monitor.
    POINT _workspaceOffset(const RECT& rect) {
        MONITORINFO info{.cbSize = sizeof(info)};
        GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &info);
        return {info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top};
    }

    bool _visibleFrame(const HWND window, RECT& frame) {
        return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)));
    }

} // anonymous namespace

namespace WindowList {

    bool IsAppWindow(const HWND window) {
        return WindowCatalog::IsAppWindow(window);
    }

    std::vector<HWND> AppWindows() {
        return WindowCatalog::AppWindows();
    }

    bool IsOnCurrentDesktop(const HWND window) {
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        return !(cloaked & DWM_CLOAKED_SHELL);
    }

    std::wstring ExeName(const HWND window) {
        return Foreground::ExeName(window);
    }

    Frame FrameOf(const HWND window) {
        const bool iconic = IsIconic(window);
        const bool zoomed = IsZoomed(window);
        if (RECT frame; !iconic && !zoomed && _visibleFrame(window, frame)) {
            return {frame, false};
        }

        WINDOWPLACEMENT placement{.length = sizeof(placement)};
        GetWindowPlacement(window, &placement);
        RECT rect = placement.rcNormalPosition;
        const POINT offset = _workspaceOffset(rect);
        OffsetRect(&rect, offset.x, offset.y);
        return {rect, zoomed || (iconic && (placement.flags & WPF_RESTORETOMAXIMIZED))};
    }

    WindowRule Capture(const HWND window) {
        const auto [rect, maximized] = FrameOf(window);
        return {.Exe = Str::WideToUtf8(ExeName(window)),
                .X = rect.left,
                .Y = rect.top,
                .Width = rect.right - rect.left,
                .Height = rect.bottom - rect.top,
                .Maximized = maximized};
    }

    void Place(const HWND window, RECT frame, const bool maximized) {
        const bool iconic = IsIconic(window);
        const bool zoomed = IsZoomed(window);

        if (RECT outer, visible; !iconic && !zoomed && GetWindowRect(window, &outer) && _visibleFrame(window, visible)) {
            if (!maximized && EqualRect(&visible, &frame)) {
                return;
            }
            frame.left -= visible.left - outer.left;
            frame.top -= visible.top - outer.top;
            frame.right += outer.right - visible.right;
            frame.bottom += outer.bottom - visible.bottom;
        } else if (zoomed && maximized
                   && MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == MonitorFromRect(&frame, MONITOR_DEFAULTTONEAREST)) {
            return;
        }

        WINDOWPLACEMENT placement{.length = sizeof(placement)};
        GetWindowPlacement(window, &placement);
        const POINT offset = _workspaceOffset(frame);
        OffsetRect(&frame, -offset.x, -offset.y);
        placement.rcNormalPosition = frame;
        placement.flags = WPF_ASYNCWINDOWPLACEMENT | (iconic && maximized ? WPF_RESTORETOMAXIMIZED : 0);

        // Restored onto the target monitor first: a maximize then lands there, and a DPI change
        // there rescales the window before the second call fixes its size
        placement.showCmd = iconic ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE;
        SetWindowPlacement(window, &placement);
        if (!iconic && maximized) {
            placement.showCmd = SW_SHOWMAXIMIZED;
        }
        SetWindowPlacement(window, &placement);
    }
}
