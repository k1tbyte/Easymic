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

    /// The tray icon still comes from here and the frame still asks for it - step 11 of
    /// docs/ARCHITECTURE.md is where a tray provider takes that over.
    HICON TrayIcon();
    void RefreshTheme();
}
