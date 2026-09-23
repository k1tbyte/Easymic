#pragma once

struct DesktopSettings;

/**
 * @brief Puts windows where the preset says: every open one on request, new ones as they open.
 *
 * Both calls are UI thread. The work itself runs on the threadpool - the Dispatcher worker only
 * runs while hotkeys do, and a placement waits out the app settling its own window first.
 */
namespace Placer {

    /// Takes the preset in effect. Window creation is only watched while a rule can act on it.
    void Load(const DesktopSettings& settings);

    /// Also creates and names the preset's desktops.
    void PlaceAll();
}
