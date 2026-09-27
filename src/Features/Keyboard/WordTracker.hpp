#pragma once

#include <memory>

#include "Core/Input/Input.hpp"

namespace Autocorrect {
    struct Runtime;
}

namespace WordTracker {
    void Register();
    /// UI thread: words are tracked with `runtime` (null forgets), the stage on while `on`.
    void Use(std::shared_ptr<const Autocorrect::Runtime> runtime, bool on);
    void ConvertWord(Input::HoldId id);
    void UndoAutoConvert(Input::HoldId id);
}
