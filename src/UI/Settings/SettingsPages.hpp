#pragma once

#include "AppConfig.hpp"

/// The frame's own pages: General, Overlay, Tray, Hotkeys and About.
namespace SettingsPages {

    /// Once, at startup, before the modules add theirs.
    void Register(AppConfig& config);

    /// A settings window opened: reads what lives outside the config.
    void Open();

    /// It closed: drops what the session cached.
    void Close();
}
