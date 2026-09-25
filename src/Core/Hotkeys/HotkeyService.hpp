#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

/**
 * @brief The hotkey stage of the input pipeline: the mask table and multi-press counting.
 *
 * The table is built on the UI thread and handed to the input thread whole by Publish, so the hook
 * never reads what the UI is writing. Nothing here runs an action: a match is posted to
 * Dispatcher. Naming a combination belongs to KeyNames, the chord arithmetic to KeyChord.
 */
namespace HotkeyService {

    struct HotkeyBinding {
        std::function<void()> onPress;
        std::function<void()> onRelease;
        /// Swallows the key instead of passing it down the hook chain. It applies to the whole
        /// combination, not to this count alone - a first press leaking through while the second
        /// was eaten is worse than either answer.
        bool block = false;
        /// Drops the release when another key went down while this one was held, so a key can act
        /// as a modifier for the next one and still do its own thing when tapped by itself.
        bool tapOnly = false;
    };

    /// Adds the stage to the pipeline. Once, before Input::Start.
    void Register();

    /// UI thread, into the table the next Publish hands over. A combination and press count can
    /// be registered once per application.
    bool RegisterHotkey(uint64_t keysMask, uint8_t presses, const HotkeyBinding& binding,
                        std::string_view app = {});
    /// How long a combination waits for another press. Only a combination with more than one
    /// bound count ever waits - everything else still fires on the press itself.
    void SetMultiPressWindow(uint16_t milliseconds);
    /// Puts the table built since the last Publish into effect and starts an empty one. The stage
    /// is on while its table holds anything; whatever the old table had waiting is dropped.
    void Publish();
    /// An empty Publish.
    void ClearHotkeys();

}
