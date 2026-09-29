#pragma once

#include <windows.h>

struct WindowRule;

/// Nothing here sends a message to the window it asks about, so a hung app cannot stall the caller.
namespace WindowList {

    bool IsOnCurrentDesktop(HWND window);

    struct Frame {
        RECT Rect;
        bool Maximized;
    };

    /// Maximized or minimized: the restore rect, borders included.
    Frame FrameOf(HWND window);

    WindowRule Capture(HWND window);

    /// Posted, never waited on. A maximized window's borders cannot be measured: a second call once restored lands exactly.
    void Place(HWND window, RECT frame, bool maximized);
}
