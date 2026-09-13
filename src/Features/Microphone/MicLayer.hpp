#pragma once

#include <windows.h>

struct MicSettings;

/**
 * @brief The microphone's pill on the overlay, and the peak meter that drives it.
 *
 * Everything here is the UI thread: the surface calls Measure, Render and Tick, and the state
 * event arrives already hopped. A layer is a plain function pointer, so the state it draws from
 * is a file static, set in Register - the same way the module holds its settings and feedback.
 */
namespace MicLayer {

    /// Once, from Mic::Register - before any window exists, which is the only ordering that
    /// matters here.
    void Register(HINSTANCE instance, MicSettings& settings);

    /// Re-reads what the pill shows now, for a caller that lays the overlay out before the posted
    /// state event would arrive.
    void Refresh();

    /// What the module's tray provider shows - the icons are shared with the pill - and the
    /// rebuild after a taskbar theme switch.
    HICON TrayIcon();
    void RefreshTheme();
}
