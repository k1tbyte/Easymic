#pragma once

struct DesktopPreset;

namespace Editor {

    /**
     * @brief The fullscreen tile editor for one preset's window rules.
     *
     * Modal: owns the message loop until Done or Cancel. With Done the preset's sized,
     * non-maximized rules carry the dragged rects; Cancel leaves it untouched.
     */
    void Run(DesktopPreset& preset);
}
