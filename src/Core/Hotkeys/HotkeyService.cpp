//
// Created by kitbyte on 04.11.2025.
//
#include "HotkeyService.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "KeyChord.hpp"
#include "Dispatcher.hpp"
#include "Foreground.hpp"
#include "Input/Input.hpp"

namespace HotkeyService {

namespace {

    constexpr std::string_view StageId = "hotkeys";

    using MaskBindings = std::vector<HotkeyBinding>;

    struct Scope {
        MaskBindings bindings;
    };

    struct AppHash {
        using is_transparent = void;
        size_t operator()(const std::string_view app) const {
            return std::hash<std::string_view>{}(app);
        }
    };

    struct MaskEntry {
        Scope global;
        std::unordered_map<std::string, Scope, AppHash, std::equal_to<>> apps;
    };

    struct Match {
        const Scope* global = nullptr;
        const Scope* app = nullptr;
        size_t count = 0;
        bool block = false;

        const HotkeyBinding* At(const size_t index) const {
            const auto bound = [index](const Scope* scope) -> const HotkeyBinding* {
                if (!scope || index >= scope->bindings.size()) {
                    return nullptr;
                }
                const auto& binding = scope->bindings[index];
                return binding.onPress || binding.onRelease || binding.onEdit ? &binding : nullptr;
            };
            if (const auto* binding = bound(app)) {
                return binding;
            }
            return bound(global);
        }

        explicit operator bool() const { return count != 0; }
    };

    struct ReleaseClaim {
        uint64_t mask;
        const HotkeyBinding* binding;
    };

    /// Everything Publish hands over, in one piece.
    struct Table {
        std::unordered_map<uint64_t, MaskEntry> Hotkeys;
        /// How long a combination waits for another press before it resolves. Only a mask with
        /// more than one bound count ever waits - everything else fires on the press.
        std::chrono::milliseconds MultiPressWindow{200};
        size_t ReleaseBindings = 0;
        /// A mask names a mouse button, or a tap has to notice a click in between. Without either
        /// the mouse hook can stay down.
        bool WantsButtons = false;
    };

    /// UI thread.
    std::unique_ptr<Table> _building = std::make_unique<Table>();

    // Input thread, from here down; Dispatcher owns deferred action locking.
    std::unique_ptr<Table> _active;
    KeyChord _chord;
    std::vector<ReleaseClaim> _releases;
    /// The last key that went down. A key still holding this slot when it is released was tapped
    /// by itself; anything else means it was held while another key was pressed.
    uint8_t _lastDownVk = 0;

    uint64_t _pendingMask = 0;
    uint8_t _pendingCount = 0;
    HWND _pendingWindow = nullptr;
    std::chrono::steady_clock::time_point _pendingDeadline{};

    void _clearPending() {
        _pendingMask = 0;
        _pendingCount = 0;
        _pendingWindow = nullptr;
    }

    const MaskEntry* _entry(const uint64_t mask) {
        const auto it = _active->Hotkeys.find(mask);
        return it == _active->Hotkeys.end() ? nullptr : &it->second;
    }

    Match _lookup(const MaskEntry* entry, const std::string_view exe) {
        if (!entry) {
            return {};
        }

        const auto found = entry->apps.find(exe);
        Match match{.global = &entry->global,
                    .app = found == entry->apps.end() ? nullptr : &found->second};
        match.count = std::max(entry->global.bindings.size(),
                               match.app ? match.app->bindings.size() : size_t{0});
        for (size_t i = 0; i < match.count; ++i) {
            if (const auto* binding = match.At(i)) {
                match.block |= binding->block;
            }
        }
        return match;
    }

    bool _contains(const uint64_t mask, const uint8_t vkCode) {
        if (const auto bit = ModifierBits[vkCode]) {
            return (mask & bit) != 0;
        }
        for (int i = 1; i < 8; ++i) {
            if (((mask >> (i * 8)) & 0xFF) == vkCode) {
                return true;
            }
        }
        return false;
    }

    void _releaseClaims(const uint8_t vkCode) {
        const bool alone = _lastDownVk == vkCode;
        for (size_t i = 0; i < _releases.size();) {
            const auto claim = _releases[i];
            if (!_contains(claim.mask, vkCode)) {
                ++i;
                continue;
            }
            if (alone || !claim.binding->tapOnly) {
                Dispatcher::Post(claim.binding->onRelease);
            }
            _releases.erase(_releases.begin() + i);
        }
    }

    /// Press, and an edit under it if the action types. In-proc: the hold starts before the key
    /// that fired it can be delivered.
    void _press(const HotkeyBinding& binding) {
        Dispatcher::Post(binding.onPress);
        if (binding.onEdit) {
            Input::Edit(binding.onEdit);
        }
    }

    void _fireOnce(const Match& entry) {
        const auto* binding = entry.At(0);
        if (binding && (entry.count == 1 || binding->onRelease)) {
            _press(*binding);
        }
    }

    /// One Dispatcher action for the press half, the edit included: a binding that waited out the
    /// multi-press window starts its hold from the worker, which Input::Edit hands over with.
    std::function<void()> _pressAction(const HotkeyBinding* binding) {
        if (!binding) {
            return {};
        }
        if (!binding->onEdit) {
            return binding->onPress;
        }
        return [press = binding->onPress, edit = binding->onEdit] {
            if (press) {
                press();
            }
            Input::Edit(edit);
        };
    }

    void _count(const Match& entry, const uint64_t mask, const HWND window) {
        _pendingMask = mask;
        _pendingWindow = window;
        ++_pendingCount;
        const auto* binding = entry.At(_pendingCount - 1);

        if (_pendingCount >= entry.count) {
            Dispatcher::CancelDeferred();
            // In-proc, like _fireOnce: an edit's hold has to catch the key that fired it
            if (binding) {
                _press(*binding);
            }
            _clearPending();
        } else {
            _pendingDeadline = std::chrono::steady_clock::now() + _active->MultiPressWindow;
            if (_pendingCount == 1 && binding && binding->onRelease && binding->onPress) {
                Dispatcher::CancelDeferred();
                Dispatcher::Post(binding->onPress);
            } else {
                Dispatcher::Defer(_pendingDeadline, _pressAction(binding));
            }
        }
    }

    bool _raiseAction(const uint8_t vkCode) {
        const uint64_t singleMask = static_cast<uint64_t>(vkCode) << 8;
        const uint64_t sequenceMask = _chord.Mask;
        const auto* singleEntry = _entry(singleMask);
        const auto* sequenceEntry = singleMask == sequenceMask ? nullptr : _entry(sequenceMask);
        const auto needsWindow = [](const MaskEntry* entry) {
            return entry && (!entry->apps.empty() || entry->global.bindings.size() > 1);
        };
        HWND window = nullptr;
        // Held for the lookups below: exe points into it
        std::shared_ptr<const Foreground::Snapshot> foreground;
        std::string_view exe;
        if (_pendingMask || needsWindow(singleEntry) || needsWindow(sequenceEntry)) {
            window = GetForegroundWindow();
            foreground = Foreground::Current();
            if (foreground && foreground->Window == window) {
                exe = foreground->Exe;
            }
        }

        const Match single = _lookup(singleEntry, exe);
        const Match sequence = _lookup(sequenceEntry, exe);
        const Match* counting = sequence.count > 1 ? &sequence : single.count > 1 ? &single : nullptr;
        const uint64_t countingMask = counting == &sequence ? sequenceMask : singleMask;

        if (_pendingMask) {
            const bool changed = window != _pendingWindow;
            const bool modifier = ModifierBits[vkCode] && _pendingMask != countingMask;
            const bool expired = std::chrono::steady_clock::now() >= _pendingDeadline;
            if (changed || expired || (!modifier && _pendingMask != countingMask)) {
                Dispatcher::FlushDeferred();
                _clearPending();
            }
        }

        bool blocked = false;
        for (const Match* entry : {&single, &sequence}) {
            if (!*entry) {
                continue;
            }
            const bool firstPress = entry != counting || _pendingCount == 0;
            if (entry == counting) {
                _count(*entry, countingMask, window);
            } else {
                _fireOnce(*entry);
            }
            if (const auto* binding = entry->At(0); firstPress && binding && binding->onRelease) {
                _releases.push_back({entry == &single ? singleMask : sequenceMask, binding});
            }
            blocked |= entry->block;
        }
        return blocked;
    }

    Input::Verdict _onKey(const Input::KeyEvent& event) {
        if (!_active || event.Repeat) {
            return Input::Verdict::Next;
        }

        if (event.Down) {
            _lastDownVk = event.Vk;
            _chord.Press(event.Vk);
            return _raiseAction(event.Vk) ? Input::Verdict::Consume : Input::Verdict::Next;
        }

        _releaseClaims(event.Vk);
        _chord.Release(event.Vk);
        return Input::Verdict::Next;
    }

    void _reset() {
        // The waiting action and the claims belong to presses this stage no longer knows about
        Dispatcher::CancelDeferred();
        _clearPending();
        _releases.clear();
        _chord = {};
        _lastDownVk = 0;
    }

    void _install(std::unique_ptr<Table> table) {
        _reset();
        // Claims are pushed from inside the hook, so their room is made here
        _releases.reserve(table->ReleaseBindings);
        _active = std::move(table);
    }

} // anonymous namespace

    void Register() {
        Input::Add({.Id = StageId, .Order = 200, .OnKey = &_onKey, .OnReset = &_reset});
    }

    bool RegisterHotkey(const uint64_t keysMask, const uint8_t presses, const HotkeyBinding& binding,
                        const std::string_view app) {
        if (!presses) {
            return false;
        }

        MaskEntry& entry = _building->Hotkeys[keysMask];
        Scope& scope = app.empty() ? entry.global : entry.apps[std::string{app}];
        MaskBindings& bindings = scope.bindings;
        if (presses <= bindings.size()) {
            const auto& previous = bindings[presses - 1];
            if (previous.onPress || previous.onRelease || previous.onEdit) {
                return false;
            }
        }

        if (bindings.size() < presses) {
            bindings.resize(presses);
        }
        bindings[presses - 1] = binding;
        _building->ReleaseBindings += binding.onRelease ? 1 : 0;
        _building->WantsButtons |= binding.tapOnly || HasMouseButton(keysMask);
        return true;
    }

    void SetMultiPressWindow(const uint16_t milliseconds) {
        _building->MultiPressWindow = std::chrono::milliseconds{milliseconds};
    }

    void Publish() {
        std::unique_ptr<Table> table = std::exchange(_building, std::make_unique<Table>());
        const bool any = !table->Hotkeys.empty();
        const Input::Wants wants = Input::WantKeys | (table->WantsButtons ? Input::WantButtons : 0);

        // Freed on the input thread too, where the last reader of the old one runs
        Input::Post([table = std::move(table)]() mutable { _install(std::move(table)); });
        if (any) {
            Input::Enable(StageId, wants);
        } else {
            Input::Disable(StageId);
        }
    }

    void ClearHotkeys() {
        _building = std::make_unique<Table>();
        Publish();
    }
}
