//
// Created by kitbyte on 04.11.2025.
//
#include "HotkeyService.hpp"

#include <array>
#include <chrono>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "Dispatcher.hpp"
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

    /// Every binding of one combination, indexed by press count minus one. A count nobody bound
    /// is a default entry, and Dispatcher::Post already ignores an empty function - so an exact
    /// match is all this has to express, and a gap in the range costs no branch of its own.
    using MaskBindings = std::vector<HotkeyBinding>;

    struct MaskEntry {
        MaskBindings bindings;
        /// True when any count bound here asked to swallow the key - see HotkeyBinding::block.
        bool block = false;
    };

    std::unordered_map<uint64_t, MaskEntry> _hotkeys;
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

    /**
     * @brief The press being counted.
     *
     * Unguarded on purpose: both low-level hooks are dispatched to the thread that installed
     * them, and everything that registers, clears or retimes a hotkey is the same UI thread, so
     * nothing here is ever touched from two places. The action waiting on the count lives in
     * Dispatcher, which does its own locking.
     */
    uint64_t _pendingMask = 0;
    uint8_t _pendingCount = 0;
    std::chrono::steady_clock::time_point _pendingDeadline{};

    void _clearPending() {
        _pendingMask = 0;
        _pendingCount = 0;
    }

    /**
     * @brief Hands one combination's action to the worker, counting presses when it has to.
     *
     * One rule decides everything: a count that has reached the highest one bound on this mask
     * fires at once, anything below it waits for the window to close. A mask with nothing but a
     * single press bound has a maximum of one, so it still fires on the press itself - the wait
     * is paid only where the user actually bound more than one count.
     *
     * The action for the count reached so far is handed to Dispatcher as it is counted, rather
     * than looked up again when the window closes. That is what keeps the press count here and
     * the waiting out of the worker's way.
     *
     * @param alone false when another key went down while this one was held - what a tapOnly
     *        binding asks about, since that is the key being used as a modifier.
     * @return true when the combination is bound to swallow the key.
     */
    bool _fire(const Keys::State state, const uint64_t mask, const bool alone) {
        const auto it = _hotkeys.find(mask);
        if (it == _hotkeys.end() || it->second.bindings.empty()) {
            return false;
        }

        const MaskEntry& entry = it->second;
        const MaskBindings& bindings = entry.bindings;

        if (bindings.size() == 1) {
            const HotkeyBinding& binding = bindings[0];
            if (state == Keys::State::KEY_PRESSED) {
                Dispatcher::Post(binding.onPress);
            } else if (alone || !binding.tapOnly) {
                Dispatcher::Post(binding.onRelease);
            }
            return entry.block;
        }

        // Counting only means anything on the press, and a mask that counts cannot also answer
        // the release: it would run the single-press action on the way up and again when the
        // window closed. Held actions keep both edges - nothing above one count is bound to them.
        if (state != Keys::State::KEY_PRESSED) {
            return entry.block;
        }

        const auto now = std::chrono::steady_clock::now();

        // Another combination, or too long a pause, ends the previous wait on its own terms
        // rather than swallowing the action the user already asked for
        if (_pendingMask && (_pendingMask != mask || now >= _pendingDeadline)) {
            Dispatcher::FlushDeferred();
            _clearPending();
        }

        _pendingMask = mask;
        _pendingCount++;

        if (_pendingCount >= bindings.size()) {
            Dispatcher::CancelDeferred();
            Dispatcher::Post(bindings[_pendingCount - 1].onPress);
            _clearPending();
        } else {
            _pendingDeadline = now + _multiPressWindow;
            Dispatcher::Defer(_pendingDeadline, bindings[_pendingCount - 1].onPress);
        }

        return entry.block;
    }

    /// The single key and the whole sequence are separate bindings, so both have to be offered
    /// the event - the operand order keeps the second _fire out of reach of short-circuiting.
    bool _raiseAction(const Keys::State state, const uint8_t vkCode) {
        const auto singleMask = static_cast<uint64_t>(vkCode) << 8;
        const bool alone = _lastDownVk == vkCode;

        bool blocked = _fire(state, singleMask, alone);

        if (singleMask != _sequenceMask) {
            blocked = _fire(state, _sequenceMask, alone) || blocked;
        }

        return blocked;
    }

    void _onKeyRelease(const uint8_t vkCode) {
        // The action fires on the mask that was still complete when the key went up
        if (!_onBindingCallback) {
            _raiseAction(Keys::State::KEY_RELEASED, vkCode);
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
            // 1. Save the current (first) byte of modifiers
            const uint8_t modifiers = _sequenceMask & 0xFF;
            // 2 Shift the existing sequence 1 byte to the left. Excluding the first byte
            uint64_t sequence = (_sequenceMask & ~0xFF) << 8;
            // 3. Add a new key to the second byte
            sequence |= (static_cast<uint64_t>(vkCode) << 8);
            // 4. Restore modifier byte
            _sequenceMask = sequence | modifiers;
        }


        if (_onBindingCallback) {
            _onBindingCallback(vkCode, Keys::State::KEY_PRESSED, _sequenceMask);
            // Skip hotkey handling if in binding mode
            return false;
        }


        return _raiseAction(Keys::State::KEY_PRESSED, vkCode);
    }

    LRESULT CALLBACK _lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode != HC_ACTION) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const auto *const pKbdStruct = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        const auto code = pKbdStruct->vkCode;
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
            _keys[code] = Keys::KEY_RELEASED;
            _onKeyRelease(static_cast<uint8_t>(code));

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

    /**
     * @brief Binds an action to a combination pressed a given number of times in a row.
     *
     * The slot is the count, so two actions on one combination no longer collide - what has to
     * be unique is the pair. Registering a high count with nothing below it leaves the counts in
     * between empty on purpose: they are the ones that must stay silent.
     */
    bool RegisterHotkey(const uint64_t keysMask, const uint8_t presses, const HotkeyBinding &binding,
                        const bool overwrite) {
        if (!presses) {
            return false;
        }

        MaskEntry& entry = _hotkeys[keysMask];
        MaskBindings& bindings = entry.bindings;

        // Checked before the resize, or a rejected registration would still raise the maximum
        // and make every count below it start waiting for a press that can never resolve
        const bool taken = presses <= bindings.size()
                           && (bindings[presses - 1].onPress || bindings[presses - 1].onRelease);
        if (taken && !overwrite) {
            return false;
        }

        if (bindings.size() < presses) {
            bindings.resize(presses);
        }

        bindings[presses - 1] = binding;
        entry.block |= binding.block;
        return true;
    }

    void SetMultiPressWindow(const uint16_t milliseconds) {
        _multiPressWindow = std::chrono::milliseconds{milliseconds};
    }


    void BindStart(const BindingCallback& callback) {
        if (!callback) {
            throw std::runtime_error("HotkeyService::BindStart: callback is null");
        }

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
            _keyboardHook = nullptr;
            _mouseHook = nullptr;
            Dispatcher::Stop();
            LOG_ERROR("SetWindowsHookEx failed: 0x%08lX", GetLastError());
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
        _hotkeys.clear();
        _clearPending();
    }

    void Dispose() {
        _keyboardHook = nullptr;
        _mouseHook = nullptr;
        Dispatcher::Stop();
        _sequenceMask = 0;
        _hotkeys.clear();
        _clearPending();
        memset(_keys, Keys::KEY_RELEASED, sizeof(_keys));
        memset(_blockedKeys, 0, sizeof(_blockedKeys));
        _lastDownVk = 0;
    }
}
