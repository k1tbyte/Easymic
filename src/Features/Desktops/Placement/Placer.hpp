#pragma once

#include <windows.h>

struct DesktopSettings;

/// UI thread only; the placing itself runs on the threadpool, waiting out an app settling its own window.
namespace Placer {

    void Load(const DesktopSettings& settings);

    /// Also creates and names the preset's desktops.
    void PlaceAll();

    /// A window has no identity that outlives it, so its rule is a guess: an app with fewer rules than
    /// windows gets a new one, otherwise the nearest is taken over. False for a window that is not an app's.
    bool Remember(DesktopSettings& settings, HWND window, int desktop);
}
