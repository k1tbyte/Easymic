#ifndef EASYMIC_ACTIONS_HPP
#define EASYMIC_ACTIONS_HPP

#include <iterator>
#include <string>

/**
 * @brief What a built-in action is, for dispatch. The table below is indexed by it.
 *
 * Adding one means a row here, a row in All and a case in the view model's switch - a switch
 * with no default, so a compiler with warnings on points at the missing case.
 */
enum class BuiltInId : int {
    ToggleMute,
    PushToTalk,
    MicVolumeUp,
    MicVolumeDown,
    ToggleBellSound,
    Count
};

/**
 * @brief One built-in action. The table below is the only place they are described.
 *
 * Key is the config key, so it has to stay stable forever - Title is what the list shows and can
 * be reworded freely.
 */
struct BuiltInAction {
    BuiltInId Id;
    const char* Key;
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
     * {volume} and {bell} when it fires - see ActionFeedback.
     */
    inline constexpr char DefaultNotification[] = "{name} triggered";

    inline constexpr int Count = static_cast<int>(BuiltInId::Count);

    inline constexpr BuiltInAction All[] = {
        // Push to talk says nothing: on a held key the text would flicker with every tap.
        // Toggle mute says nothing about state either - the indicator icon already shows it.
        {BuiltInId::ToggleMute,      "Toggle mute",       "Toggle mute",       "",     DefaultNotification, false, false},
        {BuiltInId::PushToTalk,      "Push to talk",      "Push to talk",      "",     "",                  false, true },
        {BuiltInId::MicVolumeUp,     "Mic volume up",     "Mic volume up",     "Tick", "Mic {volume}%",     true,  false},
        {BuiltInId::MicVolumeDown,   "Mic volume down",   "Mic volume down",   "Tick", "Mic {volume}%",     true,  false},
        {BuiltInId::ToggleBellSound, "Toggle bell sound", "Toggle bell sound", "Tick", "Bell {bell}",       true,  false},
    };

    static_assert(std::size(All) == static_cast<size_t>(BuiltInId::Count),
                  "BuiltInActions::All must carry one row per BuiltInId");

    /// A row's position is its id, so dispatch never has to search for it.
    consteval bool IsIndexedById() {
        for (size_t i = 0; i < std::size(All); i++) {
            if (static_cast<size_t>(All[i].Id) != i) {
                return false;
            }
        }
        return true;
    }

    static_assert(IsIndexedById(), "BuiltInActions::All must be ordered by BuiltInId");

    /// Config key -> action, for reading back what an older build wrote.
    inline const BuiltInAction* Find(const std::string& key) {
        for (const auto& action : All) {
            if (key == action.Key) {
                return &action;
            }
        }
        return nullptr;
    }
}

#endif //EASYMIC_ACTIONS_HPP
