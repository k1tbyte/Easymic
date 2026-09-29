#pragma once

struct DesktopPreset;

namespace Editor {

    /// Modal until Done or Cancel; Done writes the dragged rects into the preset's sized, non-maximized rules.
    void Run(DesktopPreset& preset);
}
