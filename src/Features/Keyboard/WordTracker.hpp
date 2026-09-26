#pragma once

#include "Core/Input/Input.hpp"

struct KeyboardSettings;

/**
 * @brief Stage kbd.text: the word being typed, as key positions, and converting it to the other
 * layout of the pair.
 *
 * Positions rather than characters, so the hook looks nothing up and converting back is
 * rendering the same keys again. The word, the hold's copy of it and the layout tables are input
 * thread state; the UI thread only builds the tables and hands them over.
 */
namespace WordTracker {

    /// Once, from Keyboard::Register, before Input::Start.
    void Register(KeyboardSettings& settings);

    /// UI thread, from an action's Make: the config being applied wants the stage on.
    void Needed();

    /// Edit lane: the word the hold was started for goes to the other layout of the pair, and the
    /// focused window follows. Nothing typed since the last conversion converts it back.
    void ConvertWord(Input::HoldId id);
}
