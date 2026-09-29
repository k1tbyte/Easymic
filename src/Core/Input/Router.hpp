#pragma once

#include <cstddef>
#include <string_view>

#include "Input.hpp"

namespace Input::Router {

    inline constexpr size_t MaxStages = 8;

    bool Add(const Stage& stage);
    /// -1 for an unknown id. Any thread once the input thread runs.
    int Find(std::string_view id);

    void Enable(size_t stage, Wants wants);
    void Disable(size_t stage);
    Wants Needed();
    /// Forgets owners and downs, not what the app saw.
    void Reset();
    void NotifyHold(HoldId id);

    /// Input thread only. Level is 0 for outside input, k + 1 for what stage k sent. True when swallowed.
    bool Key(uint8_t vk, bool down, bool button, uint8_t level);
}
