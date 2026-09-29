#include "WordTracker.hpp"

#include <algorithm>
#include <span>
#include <string_view>

#include "WordEdit.hpp"
#include "WordState.hpp"

namespace WordTracker {

namespace {

    using TypedWord::Judgement;
    using TypedWord::MaxSpaces;
    using TypedWord::TypingKeys;

    constexpr std::string_view StageId = "kbd.text";

    /// The caret left the text the words describe. Win+Space switches the layout: an erase and retype across it teaches.
    void _drop(const bool switching = false) {
        _word.Clear();
        _run.Clear();
        _held.Intact = false;
        if (!switching) {
            _erased.Text.Count = 0;
        }
    }

    bool _retyped(const int from) {
        const auto keys = [](const TypedWord::Word& word) { return std::span(word.Keys.data(), word.Count); };
        if (!_erased.Whole
            || !std::ranges::equal(keys(_erased.Text), keys(_word), {}, &Convert::Key::Vk, &Convert::Key::Vk)) {
            return false;
        }
        const int side = _runtime->SideOf(_erased.Text.Layout);
        if (side < 0 || side == from) {
            return false;
        }
        Input::Post([runtime = _runtime, erased = _erased.Text, side] { Autocorrect::Overruled(*runtime, erased, side); });
        return true;
    }

    void _onEarly(const uint32_t version, const HKL layout, const Convert::Verdict& verdict) {
        // Typed on since, tried once, or erased and retyped: its Space hears the user out
        if (!_runtime || version != _wordVersion || _held.Id || !_judging() || _word.EarlyAt || _erased.Whole
            || _word.Window != GetForegroundWindow()) {
            return;
        }
        const HWND focus = InputLanguage::FocusedWindow();
        const int from = focus && InputLanguage::LayoutOf(focus) == layout ? _runtime->SideOf(layout) : -1;
        if (from >= 0) {
            _decideHeld(from, layout, focus, &verdict, true);
        }
    }

    bool _enterWindow(const HWND window) {
        const bool allowed = _apps.Allowed(_runtime.get(), window);
        if (window != _word.Window || !allowed) {
            _drop();
            _word.Window = window;
            _undo = {};
        }
        return allowed;
    }

    void _followUndo(const bool chord, const Convert::Key key) {
        if (_undo.Original && (chord || !TypedWord::Type(_undo.Text, key))) {
            _undo = {};
        }
    }

    bool _isHeldSpace(const HWND window) {
        if (!_held.Id || !(_held.AutoSpaces || _held.Early) || _held.Word.Window != window || _word.Count) {
            return false;
        }
        if (_held.AutoSpaces < MaxSpaces) {
            ++_held.AutoSpaces;
        } else {
            _held.TypedSince = true;
        }
        return true;
    }

    void _onBackspace() {
        if (_word.Spaces) {
            --_word.Spaces;
            return;
        }
        if (!_word.Count) {
            _drop();
            return;
        }
        TypedWord::EraseKey(_word, _erased);
        _wordChanged();
    }

    void _judgeWord() {
        const HWND focus = InputLanguage::FocusedWindow();
        const HKL layout = focus ? _layoutOf(_word, focus) : nullptr;
        const int from = layout ? _runtime->SideOf(layout) : -1;
        if (from < 0 || !_runtime->Reads(layout)) {
            Autocorrect::LogSkip(*_runtime, "layout outside the pair", reinterpret_cast<UINT_PTR>(layout));
            return;
        }
        if (_retyped(from)) {
            return;
        }
        // Pinned before the hold snapshots it: the conversion reads the layout typed in
        _word.Layout = layout;
        const Convert::Verdict* ready = Judge::Ready(_wordVersion, layout);
        if (!ready || ready->WrongLayout) {
            _decideHeld(from, layout, focus, ready);
            return;
        }
        _word.Judged = _judgement(*ready);
        Judge::LogKept();
    }

    void _onSpace(const bool repeat) {
        if (_judging() && !repeat) {
            _judgeWord();
        }
        _erased.Text.Count = 0;
        if (_word.Count && _word.Spaces < MaxSpaces) {
            ++_word.Spaces;
        } else if (!_word.Count && !_held.Id) {
            _drop();
        }
    }

    void _onCaretKey(const uint8_t vk) {
        if (vk == VK_SPACE) {
            Autocorrect::LogSkip(*_runtime, "modifiers held", _modifiers);
        }
        _drop(vk == VK_SPACE);
    }

    void _onTypingKey(const Convert::Key key) {
        if (TypedWord::Rollover(_word, _run)) {
            _held.Intact = false;
        }
        TypedWord::Append(_word, _erased, key);
        _wordChanged();
    }

    void _track(const Input::KeyEvent& event) {
        if (const uint8_t bit = WordEdit::BitOf[event.Vk]) {
            _modifiers = event.Down ? _modifiers | bit : _modifiers & ~bit;
            return;
        }
        if (!event.Down) {
            return;
        }
        if (event.Vk == VK_CAPITAL) {
            if (!event.Repeat) {
                _caps = !_caps;
            }
            return;
        }
        const HWND window = GetForegroundWindow();
        if (!_enterWindow(window)) {
            return;
        }
        const bool chord = _modifiers & WordEdit::ChordBits;
        const Convert::Key key{event.Vk, (_modifiers & WordEdit::ShiftBits) != 0, _caps};
        _followUndo(chord, key);
        if (event.Vk == VK_SPACE && !chord && _isHeldSpace(window)) {
            return;
        }
        _held.TypedSince = true;
        if (!chord && event.Vk == VK_BACK) {
            _onBackspace();
        } else if (!chord && event.Vk == VK_SPACE) {
            _onSpace(event.Repeat);
        } else if (chord || !TypingKeys[event.Vk]) {
            _onCaretKey(event.Vk);
        } else {
            _onTypingKey(key);
        }
    }

    Input::Verdict _onKey(const Input::KeyEvent& event) {
        _track(event);
        return Input::Verdict::Next;
    }

    void _reset() {
        _word = {};
        _run = {};
        _held = {};
        _undo = {};
        _erased = {};
        _apps = {};
        _modifiers = 0;
        _caps = GetKeyState(VK_CAPITAL) & 1;
    }

} // anonymous namespace

    void Register() {
        Judge::Register(&_onEarly);
        Input::Add({.Id = StageId, .Order = 300, .OnKey = &_onKey, .OnReset = &_reset, .OnHold = &_onHold});
    }

    void Use(std::shared_ptr<const Autocorrect::Runtime> runtime, const bool on) {
        Input::Post([runtime = std::move(runtime)]() mutable {
            _runtime = std::move(runtime);
            Judge::Use(_runtime);
            _reset();
        });
        if (on) {
            Input::Enable(StageId, Input::WantKeys | Input::WantButtons);
        } else {
            Input::Disable(StageId);
        }
    }
}
