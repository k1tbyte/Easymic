//
// Created by kitbyte on 04.11.2025.
//
#include "HotkeyService.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "Dispatcher.hpp"
#include "Foreground.hpp"
#include "Win32Hook.hpp"
#include "definitions.h"

namespace {

constexpr std::array<uint8_t, 256> MakeModifierTable() {
    std::array<uint8_t, 256> table{};
    table[VK_LCONTROL] = Keys::Modifier::MOD_LCTRL;
    table[VK_LSHIFT]   = Keys::Modifier::MOD_LSHIFT;
    table[VK_LMENU]    = Keys::Modifier::MOD_LALT;
    table[VK_RCONTROL] = Keys::Modifier::MOD_RCTRL;
    table[VK_RSHIFT]   = Keys::Modifier::MOD_RSHIFT;
    table[VK_RMENU]    = Keys::Modifier::MOD_RALT;
    return table;
}

} // anonymous namespace

namespace HotkeyService {

    uint64_t _sequenceMask = 0;

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
                return binding.onPress || binding.onRelease ? &binding : nullptr;
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

    std::unordered_map<uint64_t, MaskEntry> _hotkeys;
    std::vector<ReleaseClaim> _releases;
    size_t _releaseBindings = 0;
    BindingCallback _onBindingCallback = nullptr;

    uint8_t _keys[256];
    /// Keys swallowed on the way down. Their release has to go the same way, or the app below is
    /// left with a key it never saw go down - and an autorepeat has to keep being swallowed too.
    uint8_t _blockedKeys[256];
    /// The last key that went down. A key still holding this slot when it is released was tapped
    /// by itself; anything else means it was held while another key was pressed.
    uint8_t _lastDownVk = 0;
    std::unique_ptr<Win32Hook> _keyboardHook = nullptr;
    std::unique_ptr<Win32Hook> _mouseHook = nullptr;
    constexpr auto ModifierTable = MakeModifierTable();

    /// How long a combination waits for another press before it resolves. Only a mask with more
    /// than one bound count ever waits - everything else fires on the press, as it always did.
    std::chrono::milliseconds _multiPressWindow{200};

    // Hooks and registration run on the UI thread; Dispatcher owns deferred action locking.
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
        const auto it = _hotkeys.find(mask);
        return it == _hotkeys.end() ? nullptr : &it->second;
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
        if (const auto bit = ModifierTable[vkCode]) {
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

    void _fireOnce(const Match& entry) {
        const auto* binding = entry.At(0);
        if (binding && (entry.count == 1 || binding->onRelease)) {
            Dispatcher::Post(binding->onPress);
        }
    }

    void _count(const Match& entry, const uint64_t mask, const HWND window) {
        _pendingMask = mask;
        _pendingWindow = window;
        ++_pendingCount;
        const auto* binding = entry.At(_pendingCount - 1);

        if (_pendingCount >= entry.count) {
            Dispatcher::CancelDeferred();
            Dispatcher::Post(binding ? binding->onPress : std::function<void()>{});
            _clearPending();
        } else {
            _pendingDeadline = std::chrono::steady_clock::now() + _multiPressWindow;
            if (_pendingCount == 1 && binding && binding->onRelease && binding->onPress) {
                Dispatcher::CancelDeferred();
                Dispatcher::Post(binding->onPress);
            } else {
                Dispatcher::Defer(_pendingDeadline, binding ? binding->onPress : std::function<void()>{});
            }
        }
    }

    bool _raiseAction(const uint8_t vkCode) {
        const uint64_t singleMask = static_cast<uint64_t>(vkCode) << 8;
        const auto* singleEntry = _entry(singleMask);
        const auto* sequenceEntry = singleMask == _sequenceMask ? nullptr : _entry(_sequenceMask);
        const auto needsWindow = [](const MaskEntry* entry) {
            return entry && (!entry->apps.empty() || entry->global.bindings.size() > 1);
        };
        HWND window = nullptr;
        std::string_view exe;
        if (_pendingMask || needsWindow(singleEntry) || needsWindow(sequenceEntry)) {
            window = GetForegroundWindow();
            const auto& cached = Foreground::CurrentOnUi();
            if (cached.Window == window) {
                exe = cached.Exe;
            }
        }

        const Match single = _lookup(singleEntry, exe);
        const Match sequence = _lookup(sequenceEntry, exe);
        const Match* counting = sequence.count > 1 ? &sequence : single.count > 1 ? &single : nullptr;
        const uint64_t countingMask = counting == &sequence ? _sequenceMask : singleMask;

        if (_pendingMask) {
            const bool changed = window != _pendingWindow;
            const bool modifier = ModifierTable[vkCode] && _pendingMask != countingMask;
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
                _releases.push_back({entry == &single ? singleMask : _sequenceMask, binding});
            }
            blocked |= entry->block;
        }
        return blocked;
    }

    void _onKeyRelease(const uint8_t vkCode) {
        if (!_onBindingCallback) {
            _releaseClaims(vkCode);
        }

        if (const auto modifierBit = ModifierTable[vkCode]) {
            _sequenceMask &= ~modifierBit;
        } else {
            const uint8_t modifiers = _sequenceMask & 0xFF;
            const uint8_t lastKeyPressed = (_sequenceMask >> 8) & 0xFF;

            // Releasing the newest key drops its byte: shift bytes 2-7 down one, keep the
            // modifier byte. Releasing anything else leaves modifiers only.
            _sequenceMask = lastKeyPressed == vkCode ? (((_sequenceMask >> 16) << 8) | modifiers) : modifiers;
        }


        if (_onBindingCallback) {
            _onBindingCallback(vkCode, Keys::State::KEY_RELEASED, _sequenceMask);
        }
    }

    bool _onKeyPress(const uint8_t vkCode) {
        _lastDownVk = vkCode;

        if (const auto modifierBit = ModifierTable[vkCode]) {
            _sequenceMask |= modifierBit;
        } else {
            const uint8_t modifiers = _sequenceMask & 0xFF;
            uint64_t sequence = (_sequenceMask & ~0xFF) << 8;
            sequence |= (static_cast<uint64_t>(vkCode) << 8);
            _sequenceMask = sequence | modifiers;
        }


        if (_onBindingCallback) {
            _onBindingCallback(vkCode, Keys::State::KEY_PRESSED, _sequenceMask);
            // Skip hotkey handling if in binding mode
            return false;
        }


        return _raiseAction(vkCode);
    }

    LRESULT CALLBACK _lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode != HC_ACTION) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const auto *const pKbdStruct = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        const auto code = pKbdStruct->vkCode;
        // The struct carries a raw DWORD, and injected input is free to put anything in it
        if (code >= 256) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (!_keys[code]) {
                _keys[code] = Keys::KEY_PRESSED;
                _blockedKeys[code] = _onKeyPress(static_cast<uint8_t>(code));
            }
            // Autorepeat lands here with the key already down, and it has to be eaten as well
            if (_blockedKeys[code]) {
                return 1;
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            // A release with no press behind it - the key went down before the hook was up, or
            // something injected it. Acting on it truncates _sequenceMask to the modifiers and
            // drops keys that really are held. The mouse proc has always guarded this.
            if (_keys[code] == Keys::KEY_PRESSED) {
                _keys[code] = Keys::KEY_RELEASED;
                _onKeyRelease(static_cast<uint8_t>(code));
            }

            if (_blockedKeys[code]) {
                _blockedKeys[code] = false;
                return 1;
            }
        }

        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }


    LRESULT CALLBACK _lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode != HC_ACTION) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        constexpr uint8_t NO_VK = 0xFF;
        uint8_t vkCode = NO_VK;
        bool keyup = false;
        const auto* const pMouseStruct = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
        // We cant do anything with this strange switch, WM_X does not match VK code
        switch (wParam) {

            case WM_XBUTTONUP:
                keyup = true;
                [[fallthrough]];
            case WM_XBUTTONDOWN: {
                const DWORD btn = (pMouseStruct->mouseData >> 16) + 4;
                if (btn < NO_VK) { vkCode = static_cast<uint8_t>(btn); }
                break;
            }

            case WM_LBUTTONUP:
                keyup = true;
                [[fallthrough]];
            case WM_LBUTTONDOWN:
                vkCode = VK_LBUTTON;
                break;

            case WM_MBUTTONUP:
                keyup = true;
                [[fallthrough]];
            case WM_MBUTTONDOWN:
                vkCode = VK_MBUTTON;
                break;

            case WM_RBUTTONUP:
                keyup = true;
                [[fallthrough]];
            case WM_RBUTTONDOWN:
                vkCode = VK_RBUTTON;
                break;
        }

        if (vkCode != NO_VK) {
            if (keyup && _keys[vkCode] == Keys::KEY_PRESSED) {
                _keys[vkCode] = Keys::KEY_RELEASED;
                _onKeyRelease(vkCode);

                if (_blockedKeys[vkCode]) {
                    _blockedKeys[vkCode] = false;
                    return 1;
                }
            } else if (!keyup && _keys[vkCode] == Keys::KEY_RELEASED) {
                _keys[vkCode] = Keys::KEY_PRESSED;
                _blockedKeys[vkCode] = _onKeyPress(vkCode);

                if (_blockedKeys[vkCode]) {
                    return 1;
                }
            }
        }

        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    bool RegisterHotkey(const uint64_t keysMask, const uint8_t presses, const HotkeyBinding& binding,
                        const std::string_view app) {
        if (!presses) {
            return false;
        }

        MaskEntry& entry = _hotkeys[keysMask];
        Scope& scope = app.empty() ? entry.global : entry.apps[std::string{app}];
        MaskBindings& bindings = scope.bindings;
        if (presses <= bindings.size()) {
            const auto& previous = bindings[presses - 1];
            if (previous.onPress || previous.onRelease) {
                return false;
            }
        }

        if (bindings.size() < presses) {
            bindings.resize(presses);
        }
        bindings[presses - 1] = binding;
        if (binding.onRelease && ++_releaseBindings > _releases.capacity()) {
            _releases.reserve(std::max(_releaseBindings, _releases.capacity() * 2));
        }
        return true;
    }

    void SetMultiPressWindow(const uint16_t milliseconds) {
        _multiPressWindow = std::chrono::milliseconds{milliseconds};
    }


    void BindStart(const BindingCallback& callback) {
        if (!callback) {
            throw std::runtime_error("HotkeyService::BindStart: callback is null");
        }

        Dispatcher::CancelDeferred();
        _clearPending();

        _onBindingCallback = callback;
        if (_sequenceMask != 0) {
            const uint8_t lastKeyPressed = (_sequenceMask >> 8) & 0xFF;
            _onBindingCallback(lastKeyPressed, Keys::State::KEY_PRESSED, _sequenceMask);
        }
    }

    void BindStop() {
        _onBindingCallback = nullptr;
    }


    bool Initialize() {
        if (_keyboardHook || _mouseHook) {
            return false;
        }

        Dispatcher::Start();

        _keyboardHook = Win32Hook::Create(WH_KEYBOARD_LL, _lowLevelKeyboardProc, nullptr, 0);
        _mouseHook = Win32Hook::Create(WH_MOUSE_LL, _lowLevelMouseProc, nullptr, 0);
        if (!_keyboardHook->IsValid() || !_mouseHook->IsValid()) {
            // Each hook kept its own error: by now the second SetWindowsHookEx has overwritten
            // the first one's, and the cleanup below overwrites both
            const DWORD error = _keyboardHook->IsValid() ? _mouseHook->LastError()
                                                         : _keyboardHook->LastError();
            _keyboardHook = nullptr;
            _mouseHook = nullptr;
            Dispatcher::Stop();
            LOG_ERROR("SetWindowsHookEx failed: 0x%08lX", error);
            return false;
        }

        return true;
    }


    bool IsHooked() {
        return _keyboardHook != nullptr;
    }

    void ClearHotkeys() {
        // The waiting action is a copy of a binding that is about to go away, so it has to go
        // with it - otherwise a combination dropped mid-count still fires once
        Dispatcher::CancelDeferred();
        _releases.clear();
        _releaseBindings = 0;
        _hotkeys.clear();
        _clearPending();
    }

    void Dispose() {
        _keyboardHook = nullptr;
        _mouseHook = nullptr;
        _onBindingCallback = nullptr;
        Dispatcher::Stop();
        _sequenceMask = 0;
        _releases.clear();
        _releaseBindings = 0;
        _hotkeys.clear();
        _clearPending();
        memset(_keys, Keys::KEY_RELEASED, sizeof(_keys));
        memset(_blockedKeys, 0, sizeof(_blockedKeys));
        _lastDownVk = 0;
    }
}
