#pragma once

struct KeyboardSettings;

/**
 * @brief The layout the user is typing in, as a pill on the overlay.
 *
 * UI thread only: the surface measures, renders and polls it. Windows sends no cross-process
 * notice when another app's layout changes, so the pill watches the foreground thread instead.
 */
namespace LayoutLayer {

    /// Once, from Keyboard::Register.
    void Register(KeyboardSettings& settings);
}
