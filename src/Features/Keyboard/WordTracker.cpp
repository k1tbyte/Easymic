#include "WordTracker.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "AppConfig.hpp"
#include "Convert/LayoutTable.hpp"
#include "Core/Lifecycle.hpp"
#include "InputLanguage.hpp"
#include "LayoutLayer.hpp"
#include "definitions.h"

namespace WordTracker {

namespace {

    constexpr std::string_view StageId = "kbd.text";
    constexpr size_t MaxKeys = 64;
    constexpr uint8_t MaxSpaces = 8;
    /// Unassigned. Tapped before an Alt or Win up, or the app takes the lone release for its menu
    constexpr WORD MaskVk = 0xE8;

    constexpr std::array<uint8_t, 8> ModifierVks = {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
                                                    VK_LMENU,  VK_RMENU,  VK_LWIN,     VK_RWIN};
    constexpr uint8_t ShiftBits = 0x03;
    constexpr uint8_t ChordBits = 0xFC;
    constexpr uint8_t AltWinBits = 0xF0;
    /// Shift and Ctrl go back down after the edit. A restored Alt or Win would open a menu when the
    /// user lets go of it.
    constexpr uint8_t RestoreBits = 0x0F;

    /// Keys that type a character in some layout. Anything else ends the word, with no table
    /// looked up in the hook.
    constexpr std::array<bool, 256> TypingKeys = [] {
        std::array<bool, 256> keys{};
        for (int vk = '0'; vk <= '9'; ++vk) {
            keys[vk] = true;
        }
        for (int vk = 'A'; vk <= 'Z'; ++vk) {
            keys[vk] = true;
        }
        for (int vk = VK_OEM_1; vk <= VK_OEM_3; ++vk) {
            keys[vk] = true;
        }
        for (int vk = VK_OEM_4; vk <= VK_OEM_8; ++vk) {
            keys[vk] = true;
        }
        keys[VK_OEM_102] = true;
        return keys;
    }();

    using Pair = std::array<Convert::LayoutTable, 2>;

    struct Word {
        std::array<Convert::Key, MaxKeys> Keys{};
        uint8_t Count = 0;
        uint8_t Spaces = 0;
        /// The side of the pair it is on screen in. -1 until a conversion says so; until then the
        /// focused window's layout does.
        int8_t Side = -1;
        HWND Window = nullptr;

        void Clear() {
            Count = 0;
            Spaces = 0;
            Side = -1;
        }
    };

    // UI thread
    KeyboardSettings* _settings = nullptr;
    bool _needed = false;

    // Input thread, from here down
    std::unique_ptr<Pair> _pair;
    Word _word;
    /// The word as the hold began, and the modifiers the app had down then.
    Word _held;
    uint8_t _heldModifiers = 0;
    Input::HoldId _heldId = 0;
    /// Any key since the hold began: the screen is no longer just the word being converted.
    bool _typedSince = false;
    uint8_t _modifiers = 0;
    bool _caps = false;

    uint8_t _modifierBit(const uint8_t vk) {
        for (size_t i = 0; i < ModifierVks.size(); ++i) {
            if (ModifierVks[i] == vk) {
                return static_cast<uint8_t>(1u << i);
            }
        }
        return 0;
    }

    Input::Verdict _onKey(const Input::KeyEvent& event) {
        if (const uint8_t bit = _modifierBit(event.Vk)) {
            _modifiers = event.Down ? _modifiers | bit : _modifiers & ~bit;
            return Input::Verdict::Next;
        }
        if (!event.Down) {
            return Input::Verdict::Next;
        }
        if (event.Vk == VK_CAPITAL) {
            if (!event.Repeat) {
                _caps = !_caps;
            }
            return Input::Verdict::Next;
        }

        _typedSince = true;
        if (const HWND window = GetForegroundWindow(); window != _word.Window) {
            _word.Clear();
            _word.Window = window;
        }
        const bool chord = _modifiers & ChordBits;
        if (event.Vk == VK_BACK && !chord) {
            if (_word.Spaces) {
                --_word.Spaces;
            } else if (_word.Count) {
                --_word.Count;
            }
        } else if (event.Vk == VK_SPACE && !chord && _word.Spaces < MaxSpaces) {
            _word.Spaces += _word.Count ? 1 : 0;
        } else if (chord || !TypingKeys[event.Vk]) {
            _word.Clear();
        } else {
            // A key after the spaces starts the next word
            if (_word.Spaces || _word.Count == MaxKeys) {
                _word.Clear();
            }
            _word.Keys[_word.Count++] = {event.Vk, (_modifiers & ShiftBits) != 0, _caps};
        }
        return Input::Verdict::Next;
    }

    void _reset() {
        _word = {};
        _heldId = 0;
        _modifiers = 0;
        // Toggle state of this thread's queue - it has no focus, so this is only a seed
        _caps = GetKeyState(VK_CAPITAL) & 1;
    }

    void _onHold(const Input::HoldId id) {
        _held = _word;
        _heldId = id;
        _heldModifiers = _modifiers;
        _typedSince = false;
        _word.Clear();
    }

    void _key(std::vector<INPUT>& edit, const WORD vk, const bool up) {
        const bool extended = vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
        INPUT input{.type = INPUT_KEYBOARD};
        input.ki = {.wVk = vk,
                    .wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)),
                    .dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | (extended ? KEYEVENTF_EXTENDEDKEY : 0u)};
        edit.push_back(input);
    }

    void _tap(std::vector<INPUT>& edit, const WORD vk) {
        _key(edit, vk, false);
        _key(edit, vk, true);
    }

    void _type(std::vector<INPUT>& edit, const wchar_t c) {
        for (const DWORD up : {0ul, static_cast<DWORD>(KEYEVENTF_KEYUP)}) {
            INPUT input{.type = INPUT_KEYBOARD};
            input.ki = {.wScan = c, .dwFlags = KEYEVENTF_UNICODE | up};
            edit.push_back(input);
        }
    }

    void _press(std::vector<INPUT>& edit, const uint8_t modifiers, const bool up) {
        for (size_t i = 0; i < ModifierVks.size(); ++i) {
            if (modifiers & (1u << i)) {
                _key(edit, ModifierVks[i], up);
            }
        }
    }

    /// Input thread: the lane posts here, so the word and the tables are never read off it.
    void _apply(const Input::HoldId id) {
        if (!_pair || !id || id != _heldId || !_held.Count) {
            return;
        }
        _heldId = 0;
        const HWND focus = InputLanguage::FocusedWindow();
        if (!focus || GetForegroundWindow() != _held.Window) {
            return;
        }

        int from = _held.Side;
        if (from < 0) {
            const HKL current = InputLanguage::LayoutOf(focus);
            from = current == (*_pair)[0].Layout() ? 0 : current == (*_pair)[1].Layout() ? 1 : -1;
            if (from < 0) {
                return;
            }
        }
        const Convert::LayoutTable& target = (*_pair)[1 - from];
        const std::span<const Convert::Key> keys{_held.Keys.data(), _held.Count};
        // One char per key on screen, or the Backspaces miss: a dead key types nothing by itself
        const std::wstring text = target.Render(keys);
        if (text.empty() || (*_pair)[from].Render(keys).empty()) {
            return;
        }

        // Held Ctrl or Alt would turn the Backspaces into word deletes and undos
        std::vector<INPUT> edit;
        edit.reserve(4 * (_held.Count + _held.Spaces) + 2 * ModifierVks.size() + 2);
        if (_heldModifiers & AltWinBits) {
            _tap(edit, MaskVk);
        }
        _press(edit, _heldModifiers, true);
        for (size_t i = 0; i < _held.Count + _held.Spaces; ++i) {
            _tap(edit, VK_BACK);
        }
        for (const wchar_t c : text) {
            _type(edit, c);
        }
        for (size_t i = 0; i < _held.Spaces; ++i) {
            _type(edit, L' ');
        }
        _press(edit, _heldModifiers & RestoreBits, false);

        // Posted ahead of the batch, and an app reads posted messages before input: the held keys
        // replay in the new layout
        InputLanguage::SwitchTo(focus, target.Layout());
        if (!Input::Commit(id, edit)) {
            InputLanguage::SwitchTo(focus, (*_pair)[from].Layout());
            return;
        }
        LayoutLayer::Requested(target.Layout());
        if (!_typedSince) {
            _word = _held;
        }
        _word.Side = static_cast<int8_t>(1 - from);
    }

    std::unique_ptr<Pair> _resolve() {
        const std::vector<InputLanguage::Layout> layouts = InputLanguage::Installed();
        const int a = InputLanguage::Find(layouts, _settings->PairA, 0);
        const int b = InputLanguage::Find(layouts, _settings->PairB, 1);
        if (a < 0 || b < 0 || layouts[a].Handle == layouts[b].Handle) {
            return nullptr;
        }
        return std::make_unique<Pair>(Pair{Convert::LayoutTable(layouts[a].Handle),
                                           Convert::LayoutTable(layouts[b].Handle)});
    }

    void _restore() {
        if (!_needed) {
            return;
        }
        auto pair = _resolve();
        if (!pair) {
            LOG_WARNING("Keyboard: the conversion pair is not two installed layouts");
            return;
        }
        // Freed on the input thread too, where the last reader of the old one runs
        Input::Post([pair = std::move(pair)]() mutable { _pair = std::move(pair); });
        Input::Enable(StageId, Input::WantKeys | Input::WantButtons);
    }

    void _suspend() {
        _needed = false;
        Input::Disable(StageId);
    }

} // anonymous namespace

    void Register(KeyboardSettings& settings) {
        _settings = &settings;
        Input::Add({.Id = StageId, .Order = 300, .OnKey = &_onKey, .OnReset = &_reset, .OnHold = &_onHold});
        Lifecycle::Restore += &_restore;
        Lifecycle::Suspend += &_suspend;
    }

    void Needed() {
        _needed = true;
    }

    void ConvertWord(const Input::HoldId id) {
        Input::Post([id] { _apply(id); });
    }
}
