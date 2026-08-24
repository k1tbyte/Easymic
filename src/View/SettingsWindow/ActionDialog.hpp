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
    std::string Title;    // dialog caption
    std::string Name;     // custom only
    std::string Command;  // custom only
    std::string Sound;    // SoundCatalog key or file path, empty means none
    uint64_t Hotkey = 0;
    bool OnRelease = false;

    bool IsCustom = false;    // shows the name and command rows
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
