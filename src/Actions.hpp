#ifndef EASYMIC_ACTIONS_HPP
#define EASYMIC_ACTIONS_HPP

#include <string>

/**
 * @brief One built-in action. The table below is the only place they are described.
 *
 * Id doubles as the config key, so it has to stay stable forever - Title is what the list shows
 * and can be reworded freely.
 */
struct BuiltInAction {
    const char* Id;
    const char* Title;
    /// SoundCatalog key prefilled when the action is configured for the first time.
    const char* DefaultSound;
    /// Notification text prefilled the same way. Empty means the action stays silent on screen.
    const char* DefaultNotification;
    /// False when the action needs no sound of its own - muting already has its own feedback.
    bool HasSound;
    /// Fires on press and release by design (push to talk), so "trigger on release" is meaningless.
    bool HoldOnly;
};

namespace BuiltInActions {

    /**
     * @brief What a notification says. {name} and {key} are resolved when the hotkey is registered,
     * {volume} and {bell} when it fires - see MainWindowViewModel.
     */
    inline constexpr char DefaultNotification[] = "{name} triggered";

    inline constexpr BuiltInAction All[] = {
        // Push to talk says nothing: on a held key the text would flicker with every tap.
        // Toggle mute says nothing about state either - the indicator icon already shows it.
        {"Toggle mute",       "Toggle mute",       "",     DefaultNotification, false, false},
        {"Push to talk",      "Push to talk",      "",     "",                  false, true },
        {"Mic volume up",     "Mic volume up",     "Tick", "Mic {volume}%",     true,  false},
        {"Mic volume down",   "Mic volume down",   "Tick", "Mic {volume}%",     true,  false},
        {"Toggle bell sound", "Toggle bell sound", "Tick", "Bell {bell}",       true,  false},
    };

    inline constexpr int Count = static_cast<int>(std::size(All));

    inline const BuiltInAction* Find(const std::string& id) {
        for (const auto& action : All) {
            if (id == action.Id) {
                return &action;
            }
        }
        return nullptr;
    }
}

#endif //EASYMIC_ACTIONS_HPP
