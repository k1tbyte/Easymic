#pragma once

#include "Core/Input/Input.hpp"

struct KeyboardSettings;

namespace WordTracker {
    void Register(KeyboardSettings& settings);
    void Needed();
    void ConvertWord(Input::HoldId id);
    void UndoAutoConvert(Input::HoldId id);
}
