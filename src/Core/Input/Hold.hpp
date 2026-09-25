#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Input.hpp"

/**
 * @brief Delivery held while an edit is in flight: the ring, what goes out next, the timeouts.
 *
 * The hold stays up until every event we sent has come back through our hook - an event already
 * queued behind the hold would otherwise reach the app ahead of the edit. What arrives meanwhile
 * is held too and sent after. Input thread only, no Win32 call: tests/RouterTest.cpp drives it.
 */
namespace Input::Hold {

    inline constexpr size_t Capacity = 64;
    inline constexpr uint64_t TimeoutMs = 150;

    /// 0 while a hold is still active, sending or waiting for its events to come back included.
    HoldId Begin(uint64_t now);
    bool Active();

    /// An event that would have been delivered. A full ring goes out as it is; a hold still
    /// waiting for its edit expires with it.
    void Take(const INPUT& event);
    /// Queues the edit ahead of everything held. False when the id is not the pending hold.
    bool Commit(HoldId id, std::span<const INPUT> edit, uint64_t now);

    /// Fills out with what to send now and counts it as in flight. False when nothing is due.
    bool Outgoing(std::vector<INPUT>& out);
    /// SendInput took count fewer than Outgoing gave it.
    void Unsent(size_t count);
    /// One of the events we sent passed our hook.
    void Arrived();

    /// A hold past its deadline goes out without an edit; events that never came back (the hook
    /// went down, UIPI ate them) stop being waited for.
    void Tick(uint64_t now);
}
