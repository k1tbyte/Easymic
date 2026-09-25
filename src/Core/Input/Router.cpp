#include "Router.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <vector>

namespace Input::Router {

namespace {

    constexpr uint8_t NoOwner = 0xFF;

    struct StageState {
        bool Enabled = false;
        Wants Wanted = 0;
        /// Keys this stage saw go down and not come up - the only ups it is given.
        std::bitset<256> Down;
    };

    /// Per level, because a remap that eats a key may send the same key back, and that one is
    /// not a repeat of what it ate.
    using Owners = std::array<std::array<uint8_t, 256>, MaxStages + 1>;

    std::vector<Stage> _stages;
    std::array<StageState, MaxStages> _state;
    /// The stage that consumed a key's down; its repeats and its up go there alone.
    Owners _owners = [] {
        Owners owners;
        for (auto& level : owners) {
            level.fill(NoOwner);
        }
        return owners;
    }();

    Verdict _call(const size_t stage, const uint8_t vk, const bool down) {
        StageState& state = _state[stage];
        const bool repeat = down && state.Down[vk];
        state.Down[vk] = down;
        return _stages[stage].OnKey({.Vk = vk, .Down = down, .Repeat = repeat});
    }

} // anonymous namespace

bool Add(const Stage& stage) {
    if (_stages.size() == MaxStages || !stage.OnKey) {
        return false;
    }
    _stages.insert(std::ranges::upper_bound(_stages, stage.Order, {}, &Stage::Order), stage);
    return true;
}

int Find(const std::string_view id) {
    const auto it = std::ranges::find(_stages, id, &Stage::Id);
    return it == _stages.end() ? -1 : static_cast<int>(it - _stages.begin());
}

void Enable(const size_t stage, const Wants wants) {
    StageState& state = _state[stage];
    state.Enabled = true;
    state.Wanted = wants;
    state.Down.reset();
    if (_stages[stage].OnReset) {
        _stages[stage].OnReset();
    }
}

void Disable(const size_t stage) {
    _state[stage].Enabled = false;
    _state[stage].Down.reset();
}

Wants Needed() {
    Wants needed = 0;
    for (size_t i = 0; i < _stages.size(); ++i) {
        if (_state[i].Enabled) {
            needed |= _state[i].Wanted;
        }
    }
    return needed;
}

void Reset() {
    for (auto& level : _owners) {
        level.fill(NoOwner);
    }
    for (size_t i = 0; i < _stages.size(); ++i) {
        _state[i].Down.reset();
        if (_state[i].Enabled && _stages[i].OnReset) {
            _stages[i].OnReset();
        }
    }
}

void NotifyHold(const HoldId id) {
    for (size_t i = 0; i < _stages.size(); ++i) {
        if (_state[i].Enabled && _stages[i].OnHold) {
            _stages[i].OnHold(id);
        }
    }
}

bool Key(const uint8_t vk, const bool down, const bool button, const uint8_t level) {
    if (level >= _owners.size()) {
        return false;
    }

    if (const uint8_t owner = _owners[level][vk]; owner != NoOwner) {
        if (!down) {
            _owners[level][vk] = NoOwner;
        }
        // Everyone up to the owner saw the down, so everyone there gets the rest; a disabled
        // stage is not called, but the app never saw the down, so the rest stays eaten
        for (size_t i = level; i <= owner; ++i) {
            if (_state[i].Enabled && _state[i].Down[vk]) {
                _call(i, vk, down);
            }
        }
        return true;
    }

    const Wants kind = button ? WantButtons : WantKeys;
    for (size_t i = level; i < _stages.size(); ++i) {
        const StageState& state = _state[i];
        if (!state.Enabled || !(state.Wanted & kind) || (!down && !state.Down[vk])) {
            continue;
        }

        const bool repeat = down && state.Down[vk];
        const Verdict verdict = _call(i, vk, down);
        if (verdict == Verdict::Deliver) {
            return false;
        }
        if (verdict == Verdict::Consume && down) {
            if (!repeat) {
                _owners[level][vk] = static_cast<uint8_t>(i);
            }
            return true;
        }
    }
    return false;
}

}
