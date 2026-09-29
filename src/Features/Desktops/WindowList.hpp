#pragma once

#include <windows.h>

struct WindowRule;

/**
 * @brief Top-level windows as the user sees them: which count, what they run, where they sit.
 *
 * Nothing here sends a message to the window it asks about, so a hung app cannot stall the
 * caller - which may be the UI thread, the one the low-level hooks run on.
 */
namespace WindowList {

    /// Not cloaked by the shell for sitting on another desktop. A pinned window is on every one.
    bool IsOnCurrentDesktop(HWND window);

    struct Frame {
        RECT Rect;
        bool Maximized;
    };

    /// The visible frame, without the invisible resize borders. For a maximized or minimized
    /// window, its restore rect - borders included, close enough to pick the monitor and size.
    Frame FrameOf(HWND window);

    /// The window as a rule: its app and where it sits now.
    WindowRule Capture(HWND window);

    /**
     * @brief Moves the window's visible frame to the rect, or maximizes it on that rect's monitor.
     *
     * Posted to the window's thread, never waited on, and a no-op when it is already there. A
     * maximized window's borders cannot be measured, so the first call on one lands a few pixels
     * off and a second call once it is restored lands exactly.
     */
    void Place(HWND window, RECT frame, bool maximized);
}
