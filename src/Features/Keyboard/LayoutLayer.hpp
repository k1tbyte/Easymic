#pragma once

#include <windows.h>

struct KeyboardSettings;

/**
 * @brief The layout the user is typing in, as a pill on the overlay.
 *
 * UI thread only: the surface measures and renders it, and WinEvents re-read it while the pill
 * is on. Nothing polls.
 */
namespace LayoutLayer {

    /// Once, from Keyboard::Register.
    void Register(KeyboardSettings& settings);

    /// A switch this app asked for, shown before the window has even taken it. Any thread.
    void Requested(HKL layout);
}
