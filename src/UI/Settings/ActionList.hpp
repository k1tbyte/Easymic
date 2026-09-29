#pragma once

#include <windows.h>

#include "AppConfig.hpp"

/// The Hotkeys page's table of bindings: a double-click edits one, the last row adds another.
namespace ActionList {

    /// A Custom row's Create: fills the cell and edits cfg's bindings for as long as the page lives.
    void Create(HWND page, const RECT& cell, AppConfig& cfg);
}
