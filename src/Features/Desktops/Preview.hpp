#pragma once

#include <vector>
#include <windows.h>

struct WindowRule;

namespace Preview {

    /// The monitors to scale inside area, each rule with a size a tile named after its app.
    void Paint(HDC dc, const RECT& area, const std::vector<WindowRule>& windows, float textSize);
}
