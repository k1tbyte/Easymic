#ifndef EASYMIC_ACTIONDIALOG_HPP
#define EASYMIC_ACTIONDIALOG_HPP

#include <cstdint>
#include <set>
#include <string>
#include <windows.h>

/**
 * @brief One editor for every action, built-in or custom.
 *
 * The flags decide which rows exist - a built-in has no command line, push to talk has no
 * "trigger on release", and an action whose feedback is covered elsewhere has no sound row.
 * Everything else, including future ones like debounce, stays common to all of them.
 */
struct ActionEdit {
    std::string Title;        // dialog caption
    std::string Name;
    std::string Command;      // command actions only
    /// What a built-in was configured with. The label doubles as the switch: empty means this
    /// action takes no argument and the row is not there at all.
    std::string Args;
    std::string ArgsLabel;
    std::string ArgsHint;
    std::string Sound;        // SoundCatalog key or file path, empty means none
    uint8_t SoundVolume = 100; // 0-100, this action's own level
    std::string Notification; // overlay text, {token} aware
    uint64_t Hotkey = 0;
    bool OnRelease = false;
    /// How many presses of the combination in a row run this action.
    uint8_t Presses = 1;
    /// Swallows the combination so nothing below Easymic sees it.
    bool Block = false;
    /// Release only: the key has to have been tapped by itself, not held as a modifier.
    bool TapOnly = false;
    bool ShowNotification = false;

    bool IsCustom = false;    // shows the command row
    bool HasSound = true;     // shows the sound row
    bool HoldOnly = false;    // hides "trigger on release" - the action needs both edges
    bool AllowDelete = false; // shows the Delete button

    bool Deleted = false;     // out: the user pressed Delete
};

namespace ActionDialog {
    /// @return true when the action must be saved
    bool Show(HINSTANCE hInstance, HWND owner, ActionEdit& action, std::set<std::string>& recentSounds);
}

#endif //EASYMIC_ACTIONDIALOG_HPP
