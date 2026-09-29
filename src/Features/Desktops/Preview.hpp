#pragma once

#include <vector>
#include <windows.h>

struct WindowRule;

namespace Preview {

    void Paint(HDC dc, const RECT& area, const std::vector<WindowRule>& windows, float textSize);
}
