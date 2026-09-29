#pragma once

#include <windows.h>

struct AppConfig;

namespace ExcludedApps {
    void Create(HWND page, const RECT& cell, int id, AppConfig& config);
}
