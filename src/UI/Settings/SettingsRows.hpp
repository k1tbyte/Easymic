#pragma once

#include <span>
#include <windows.h>

#include "Core/SettingsHost.hpp"

/// Turns a page's rows into controls, and keeps every control showing its field.
namespace SettingsRows {

    /// Lays the rows out down the page in dialog units, so the page's own font sets the scale.
    void Build(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg);

    /// WM_COMMAND or WM_HSCROLL from a page Build filled: writes the field, runs the row's hook,
    /// then brings every control back in line with the config.
    void Handle(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg, UINT message,
                WPARAM wParam, LPARAM lParam);

    /// A child made the way the grid makes its own - the page's font, cell in dialog units - for a
    /// Custom row to fill its cell with.
    HWND Control(HWND page, const wchar_t* cls, const wchar_t* text, DWORD style, RECT cell, int id,
                 DWORD exStyle = 0);
}
