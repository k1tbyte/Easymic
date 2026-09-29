#pragma once

#include <span>
#include <windows.h>

#include "Core/SettingsHost.hpp"

namespace SettingsRows {

    /// Rows go down the page in dialog units, so the page's font sets the scale; a taller page scrolls itself.
    void Build(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg);

    void Handle(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg, UINT message,
                WPARAM wParam, LPARAM lParam);
}
