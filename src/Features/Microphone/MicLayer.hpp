#pragma once

#include <windows.h>

struct MicSettings;

/// UI thread only.
namespace MicLayer {

    void Register(HINSTANCE instance, MicSettings& settings);

    void Refresh();

    HICON TrayIcon();
    void RefreshTheme();
}
