#include "HotkeyService.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "KeyChord.hpp"
#include "Dispatcher.hpp"
#include "Windowing/Foreground.hpp"
#include "Input/Input.hpp"

namespace HotkeyService {

namespace {

    constexpr std::string_view StageId = "hotkeys";

    struct Scope {
        std::vector<HotkeyBinding> bindings;
        bool block = false;
    };

    using AppScope = std::pair<std::string, Scope>;

    struct MaskEntry {
        Scope global;
        std::vector<AppScope> apps;
    };

    bool _bound(const HotkeyBinding& binding) {
        return binding.onPress || binding.onRelease || binding.onEdit;
    }

    struct Match {
        const Scope* global = nullptr;
        const Scope* app = nullptr;
        size_t count = 0;
        bool block = false;

        /// The app's binding for that press count, else the global one.
        const HotkeyBinding* ForPress(const size_t index) const {
            const auto bound = [index](const Scope* scope) -> const HotkeyBinding* {
                if (!scope || index >= scope->bindings.size() || !_bound(scope->bindings[index])) {
                    return nullptr;
                }
                return &scope->bindings[index];
            };
            const auto* binding = bound(app);
            return binding ? binding : bound(global);
        }

        explicit operator bool() const { return count != 0; }
    };

    struct Candidate {
        Match match;
        uint64_t mask;
    };

    struct Focus {
        HWND window = nullptr;
        std::shared_ptr<const Foreground::Snapshot> snapshot;
        std::string_view exe;
    };

    struct ReleaseClaim {
        uint64_t mask;
        const HotkeyBinding* binding;
    };

    struct Table {
        std::unordered_map<uint64_t, MaskEntry> Hotkeys;
        std::chrono::milliseconds MultiPressWindow{200};
        size_t ReleaseBindings = 0;
        /// A mask names a mouse button, or a tap must notice a click in between: else the mouse hook stays down.
        bool WantsButtons = false;
    };

    // UI thread
    std::unique_ptr<Table> _building = std::make_unique<Table>();

    // Input thread, from here down
    std::unique_ptr<Table> _active;
    KeyChord _chord;
    std::vector<ReleaseClaim> _releases;
    /// A key still last down when released was tapped alone.
    uint8_t _lastDownVk = 0;

    uint64_t _pendingMask = 0;
    uint8_t _pendingCount = 0;
    HWND _pendingWindow = nullptr;
    std::chrono::steady_clock::time_point _pendingDeadline{};
    /// Fires on the input thread: the action worker can be busy past an edit's hold timeout.
    const HotkeyBinding* _pendingPress = nullptr;
    PTP_TIMER _timer = nullptr;

    void _clearPending() {
        _pendingMask = 0;
        _pendingCount = 0;
        _pendingWindow = nullptr;
        _pendingPress = nullptr;
        SetThreadpoolTimer(_timer, nullptr, 0, 0);
    }

    /// In-proc: an edit's hold must start before the key that fired it is delivered.
    void _press(const HotkeyBinding& binding) {
        if (!binding.onEdit) {
            Dispatcher::Post(binding.onPress);
            return;
        }
        Input::Work landed;
        if (binding.onPress) {
            landed = [press = binding.onPress] { Dispatcher::Post(press); };
        }
        Input::Edit(binding.onEdit, std::move(landed));
    }

    void _firePending() {
        const HotkeyBinding* binding = _pendingPress;
        _clearPending();
        if (binding) {
            _press(*binding);
        }
    }

    void _arm(const std::chrono::steady_clock::duration delay) {
        // Negative is relative, in 100 ns units
        const int64_t due = -std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>>(delay).count();
        FILETIME at{.dwLowDateTime = static_cast<DWORD>(due), .dwHighDateTime = static_cast<DWORD>(due >> 32)};
        SetThreadpoolTimer(_timer, &at, 0, 0);
    }

    void _due() {
        if (!_pendingMask) {
            return;
        }
        if (const auto left = _pendingDeadline - std::chrono::steady_clock::now(); left > left.zero()) {
            _arm(left);
        } else {
            _firePending();
        }
    }

    const MaskEntry* _entry(const uint64_t mask) {
        const auto it = _active->Hotkeys.find(mask);
        return it == _active->Hotkeys.end() ? nullptr : &it->second;
    }

    Match _match(const Scope& global, const Scope* app) {
        return {.global = &global,
                .app = app,
                .count = std::max(global.bindings.size(), app ? app->bindings.size() : size_t{0}),
                .block = (app ? app : &global)->block};
    }

    Match _lookup(const MaskEntry* entry, const std::string_view exe) {
        if (!entry) {
            return {};
        }
        const auto found = std::ranges::find(entry->apps, exe, &AppScope::first);
        return _match(entry->global, found == entry->apps.end() ? nullptr : &found->second);
    }

    bool _blocks(const Match& match) {
        for (size_t i = 0; i < match.count; ++i) {
            if (const auto* binding = match.ForPress(i); binding && binding->block) {
                return true;
            }
        }
        return false;
    }

    void _releaseClaims(const uint8_t vk) {
        const bool alone = _lastDownVk == vk;
        size_t kept = 0;
        for (const ReleaseClaim& claim : _releases) {
            if (!Contains(claim.mask, vk)) {
                _releases[kept++] = claim;
            } else if (alone || !claim.binding->tapOnly) {
                Dispatcher::Post(claim.binding->onRelease);
            }
        }
        _releases.resize(kept);
    }

    void _fireOnce(const Match& match) {
        const auto* binding = match.ForPress(0);
        if (binding && (match.count == 1 || binding->onRelease)) {
            _press(*binding);
        }
    }

    void _count(const Match& match, const uint64_t mask, const HWND window) {
        _pendingMask = mask;
        _pendingWindow = window;
        ++_pendingCount;
        const auto* binding = match.ForPress(_pendingCount - 1);

        if (_pendingCount >= match.count) {
            _clearPending();
            if (binding) {
                _press(*binding);
            }
            return;
        }

        _pendingDeadline = std::chrono::steady_clock::now() + _active->MultiPressWindow;
        _arm(_active->MultiPressWindow);
        // A first press that also acts on its release fires at once; anything else waits
        if (_pendingCount == 1 && binding && binding->onRelease && binding->onPress) {
            _pendingPress = nullptr;
            Dispatcher::Post(binding->onPress);
        } else {
            _pendingPress = binding;
        }
    }

    Focus _focus(const MaskEntry* single, const MaskEntry* sequence) {
        const auto perApp = [](const MaskEntry* entry) { return entry && !entry->apps.empty(); };
        const auto counts = [](const MaskEntry* entry) { return entry && entry->global.bindings.size() > 1; };
        const bool apps = perApp(single) || perApp(sequence);

        Focus focus;
        if (!apps && !_pendingMask && !counts(single) && !counts(sequence)) {
            return focus;
        }
        focus.window = GetForegroundWindow();
        if (!apps) {
            return focus;
        }
        // Current() takes a lock, so only a per-app binding reads it
        focus.snapshot = Foreground::Current();
        if (focus.snapshot && focus.snapshot->Window == focus.window) {
            focus.exe = focus.snapshot->Exe;
        }
        return focus;
    }

    void _settlePending(const uint8_t vk, const HWND window, const uint64_t countingMask) {
        if (!_pendingMask) {
            return;
        }
        const bool otherCombination = _pendingMask != countingMask;
        const bool expired = std::chrono::steady_clock::now() >= _pendingDeadline;
        // A modifier pressed on the way to another combination keeps the wait
        if (window != _pendingWindow || expired || (otherCombination && !ModifierBits[vk])) {
            _firePending();
        }
    }

    bool _dispatch(const Candidate& candidate, const bool counting, const HWND window) {
        const Match& match = candidate.match;
        if (!match) {
            return false;
        }
        const bool firstPress = !counting || _pendingCount == 0;
        if (counting) {
            _count(match, candidate.mask, window);
        } else {
            _fireOnce(match);
        }
        if (const auto* binding = match.ForPress(0); firstPress && binding && binding->onRelease) {
            _releases.push_back({candidate.mask, binding});
        }
        return match.block;
    }

    bool _onDown(const uint8_t vk) {
        const uint64_t singleMask = SingleKey(vk);
        const MaskEntry* singleEntry = _entry(singleMask);
        const MaskEntry* sequenceEntry = singleMask == _chord.Mask ? nullptr : _entry(_chord.Mask);
        if (!singleEntry && !sequenceEntry && !_pendingMask) {
            return false;
        }

        const Focus focus = _focus(singleEntry, sequenceEntry);
        const std::array<Candidate, 2> candidates{{{_lookup(singleEntry, focus.exe), singleMask},
                                                   {_lookup(sequenceEntry, focus.exe), _chord.Mask}}};
        const Candidate* counting = candidates[1].match.count > 1   ? &candidates[1]
                                    : candidates[0].match.count > 1 ? &candidates[0]
                                                                    : nullptr;
        _settlePending(vk, focus.window, counting ? counting->mask : singleMask);

        bool blocked = false;
        for (const Candidate& candidate : candidates) {
            blocked |= _dispatch(candidate, &candidate == counting, focus.window);
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
            return _onDown(event.Vk) ? Input::Verdict::Consume : Input::Verdict::Next;
        }

        _releaseClaims(event.Vk);
        _chord.Release(event.Vk);
        return Input::Verdict::Next;
    }

    void _reset() {
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

    Scope& _scope(MaskEntry& entry, const std::string_view app) {
        if (app.empty()) {
            return entry.global;
        }
        if (const auto found = std::ranges::find(entry.apps, app, &AppScope::first); found != entry.apps.end()) {
            return found->second;
        }
        return entry.apps.emplace_back(std::string{app}, Scope{}).second;
    }

} // anonymous namespace

    void Register() {
        Input::Add({.Id = StageId, .Order = 200, .OnKey = &_onKey, .OnReset = &_reset});
        _timer = CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, void*, PTP_TIMER) {
            Input::Post(&_due);
        }, nullptr, nullptr);
    }

    bool RegisterHotkey(const uint64_t keysMask, const uint8_t presses, const HotkeyBinding& binding,
                        const std::string_view app) {
        if (!presses) {
            return false;
        }

        auto& bindings = _scope(_building->Hotkeys[keysMask], app).bindings;
        if (presses <= bindings.size() && _bound(bindings[presses - 1])) {
            return false;
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
        for (auto& [mask, entry] : table->Hotkeys) {
            entry.global.block = _blocks(_match(entry.global, nullptr));
            for (auto& [app, scope] : entry.apps) {
                scope.block = _blocks(_match(entry.global, &scope));
            }
        }
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
