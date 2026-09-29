#include "Hold.hpp"

#include <algorithm>
#include <array>

namespace Input::Hold {

namespace {

    std::array<INPUT, Capacity> _ring;
    size_t _held = 0;
    /// Reserved once: nothing allocates on the input thread when a hold starts.
    std::vector<INPUT> _outbox = [] {
        std::vector<INPUT> outbox;
        outbox.reserve(Capacity * 4);
        return outbox;
    }();
    HoldId _pending = 0;
    HoldId _last = 0;
    size_t _inFlight = 0;
    uint64_t _deadline = 0;

    void _spill() {
        _outbox.insert(_outbox.end(), _ring.begin(), _ring.begin() + _held);
        _held = 0;
    }

} // anonymous namespace

HoldId Begin(const uint64_t now) {
    if (Active()) {
        return 0;
    }
    _pending = ++_last ? _last : ++_last;
    _deadline = now + TimeoutMs;
    return _pending;
}

bool Active() {
    return _pending || _held || _inFlight || !_outbox.empty();
}

void Take(const INPUT& event, const uint64_t now) {
    // WM_TIMER arrives only when the queue is idle, so input checks the deadline too.
    Tick(now);
    _ring[_held++] = event;
    if (_held == Capacity) {
        _pending = 0;
        _spill();
    }
}

bool Commit(const HoldId id, const std::span<const INPUT> edit, const uint64_t now) {
    Tick(now);
    if (!id || id != _pending) {
        return false;
    }
    _pending = 0;
    _outbox.assign(edit.begin(), edit.end());
    _spill();
    _deadline = now + TimeoutMs;
    return true;
}

bool Outgoing(std::vector<INPUT>& out) {
    if (_pending) {
        return false;
    }
    // Sent events queue behind those in flight, so order holds; the ring waits to go as one batch.
    if (!_inFlight) {
        _spill();
    }
    out.clear();
    if (_outbox.empty()) {
        return false;
    }
    out.swap(_outbox);
    _inFlight += out.size();
    return true;
}

void Unsent(const size_t count) {
    _inFlight -= std::min(count, _inFlight);
}

void Arrived() {
    if (_inFlight) {
        --_inFlight;
    }
}

void Tick(const uint64_t now) {
    if (!Active() || now < _deadline) {
        return;
    }
    if (_pending) {
        _pending = 0;
        _spill();
    } else {
        _inFlight = 0;
    }
    _deadline = now + TimeoutMs;
}

void Flush() {
    _pending = 0;
    _inFlight = 0;
    _spill();
}

}
