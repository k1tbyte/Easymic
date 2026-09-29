#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace HotkeyService {

    struct HotkeyBinding {
        std::function<void()> onPress;
        std::function<void()> onRelease;
        /// Runs on the edit lane under a hold (Input::Edit); onPress then runs once the edit lands.
        std::function<void()> onEdit;
        /// Applies to the whole combination, not to one press count.
        bool block = false;
        /// Drops the release when another key went down meanwhile.
        bool tapOnly = false;
    };

    /// Once, before Input::Start.
    void Register();

    /// UI thread. False when that combination, press count and app is already bound.
    bool RegisterHotkey(uint64_t keysMask, uint8_t presses, const HotkeyBinding& binding,
                        std::string_view app = {});
    /// Only a combination with more than one bound count ever waits for another press.
    void SetMultiPressWindow(uint16_t milliseconds);
    /// Activates what was registered since the last Publish; a press waiting under the old table is dropped.
    void Publish();
    void ClearHotkeys();

}
