#pragma once

#include <cstddef>
#include <string_view>

#include "Input.hpp"

/**
 * @brief The pipeline without the hooks: which stage sees an event, who owns a key, what is
 * swallowed. Input thread only, and no Win32 call, so a harness can drive it with made-up events.
 */
namespace Input::Router {

    inline constexpr size_t MaxStages = 8;

    /// Sorted in by Order. False when full or the stage has no OnKey.
    bool Add(const Stage& stage);
    /// -1 for an id nobody added. Any thread once the input thread runs: the list is fixed by then.
    int Find(std::string_view id);

    void Enable(size_t stage, Wants wants);
    void Disable(size_t stage);
    /// What the enabled stages want between them - the hooks follow it.
    Wants Needed();
    /// Nothing held so far can be trusted: every owner and every down is forgotten.
    void Reset();
    /// Tells every enabled stage a hold began.
    void NotifyHold(HoldId id);

    /**
     * @brief Walks one event through the stages.
     *
     * @param level 0 for physical input and anyone else's injection; k + 1 when stage k sent it,
     * which only the stages after k see.
     * @return true when the event is swallowed.
     */
    bool Key(uint8_t vk, bool down, bool button, uint8_t level);
}
