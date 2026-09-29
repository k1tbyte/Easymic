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
        std::bitset<256> Down;
    };

    /// Per level: a remap may send back the key it ate, and that is no repeat of it.
    using Owners = std::array<std::array<uint8_t, 256>, MaxStages + 1>;

    std::vector<Stage> _stages;
    std::array<StageState, MaxStages> _state;
    Owners _owners = [] {
        Owners owners;
        for (auto& level : owners) {
            level.fill(NoOwner);
        }
        return owners;
    }();
    /// Survives Enable and Reset: a consumed repeat of a down the app saw must not take the up.
    std::array<std::bitset<256>, MaxStages + 1> _delivered;

    Verdict _call(const size_t stage, const uint8_t vk, const bool down) {
        StageState& state = _state[stage];
        const bool repeat = down && state.Down[vk];
        state.Down[vk] = down;
        return _stages[stage].OnKey({.Vk = vk, .Down = down, .Repeat = repeat});
    }

    bool _offer(const uint8_t vk, const bool down, const bool button, const uint8_t level) {
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
                if (!repeat && !_delivered[level][vk]) {
                    _owners[level][vk] = static_cast<uint8_t>(i);
                }
                return true;
            }
        }
        return false;
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

    if (!down) {
        _delivered[level][vk] = false;
    }
    if (const uint8_t owner = _owners[level][vk]; owner != NoOwner) {
        if (!down) {
            _owners[level][vk] = NoOwner;
        }
        // A disabled owner is skipped, yet the app never saw the down: the rest stays eaten
        for (size_t i = level; i <= owner; ++i) {
            if (_state[i].Enabled && _state[i].Down[vk]) {
                _call(i, vk, down);
            }
        }
        return true;
    }

    const bool swallowed = _offer(vk, down, button, level);
    if (down && !swallowed) {
        _delivered[level][vk] = true;
    }
    return swallowed;
}

}
