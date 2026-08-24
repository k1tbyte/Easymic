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
    /// False when the action needs no sound of its own - muting already has its own feedback.
    bool HasSound;
    /// Fires on press and release by design (push to talk), so "trigger on release" is meaningless.
    bool HoldOnly;
};

namespace BuiltInActions {

    inline constexpr BuiltInAction All[] = {
        {"Toggle mute",       "Toggle mute",       "",     false, false},
        {"Push to talk",      "Push to talk",      "",     false, true },
        {"Mic volume up",     "Mic volume up",     "Tick", true,  false},
        {"Mic volume down",   "Mic volume down",   "Tick", true,  false},
        {"Toggle bell sound", "Toggle bell sound", "Tick", true,  false},
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
