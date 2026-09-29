#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Input.hpp"

namespace Input::Hold {

    inline constexpr size_t Capacity = 64;
    inline constexpr uint64_t TimeoutMs = 150;

    /// 0 while a hold is active, in flight and waiting for echoes included.
    HoldId Begin(uint64_t now);
    bool Active();

    void Take(const INPUT& event, uint64_t now);
    /// False when the id is not the pending hold or its deadline passed.
    bool Commit(HoldId id, std::span<const INPUT> edit, uint64_t now);

    /// Fills out with what to send now and counts it as in flight.
    bool Outgoing(std::vector<INPUT>& out);
    void Unsent(size_t count);
    void Arrived();

    void Tick(uint64_t now);
    /// Ends the hold: all held is due and what is in flight is not waited for. The batch Outgoing
    /// hands out next needs Unsent to be given up too.
    void Flush();
}
