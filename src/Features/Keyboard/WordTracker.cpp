#include "WordTracker.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Autocorrect.hpp"
#include "InputLanguage.hpp"
#include "KeyboardExclusions.hpp"
#include "Judge.hpp"
#include "Learning.hpp"
#include "LayoutLayer.hpp"
#include "TypedWord.hpp"
#include "WordEdit.hpp"

namespace WordTracker {

namespace {

    using Autocorrect::Runtime;
    using TypedWord::Judgement;
    using TypedWord::MaxKeys;
    using TypedWord::MaxSpaces;
    using TypedWord::TypingKeys;
    using TypedWord::Word;

    constexpr std::string_view StageId = "kbd.text";

    struct Undo {
        HKL Original = nullptr;
        HKL Corrected = nullptr;
        HWND Focus = nullptr;
        HWND Window = nullptr;
    };

    // Input thread
    std::shared_ptr<const Runtime> _runtime;
    KeyboardExclusions::Filter _apps;
    Word _word;
    /// The version of the word's keys the judge was told: a verdict on another version is stale.
    uint32_t _gen = 0;
    /// Undecided words right before `_word`, joined: a sure fix of the next word converts them too.
    Word _run;
    Word _held;
    Word _heldRun;
    /// Only the next word was typed since the hold: the held word still sits right before `_word`.
    bool _heldIntact = false;
    HWND _heldFocus = nullptr;
    uint8_t _heldModifiers = 0;
    Input::HoldId _heldId = 0;
    uint8_t _autoSpaces = 0;
    bool _typedSince = false;
    /// The hold converts the word mid-word: keys typed during it are its rest, or its spaces.
    bool _heldEarly = false;
    Undo _undo;
    /// A judged word being erased: its keys typed again from the other side teach.
    Word _erased;
    bool _erasedWhole = false;
    uint8_t _modifiers = 0;
    bool _caps = false;
    /// The layout each pair side was last typed in: a conversion goes there, not to the pair's.
    std::array<HKL, 2> _last{};

    HKL _layoutOf(const Word& word, const HWND focus) {
        return word.Layout ? word.Layout : InputLanguage::LayoutOf(focus);
    }

    /// The pair side a layout types for, noted as that side's last. -1: none.
    int _sideOf(const Runtime& runtime, const HKL layout) {
        const int side = runtime.SideOf(layout);
        if (side >= 0) {
            _last[side] = layout;
        }
        return side;
    }

    const Convert::LayoutTable& _target(const Runtime& runtime, const int from) {
        const Convert::LayoutTable* last = runtime.Find(_last[1 - from]);
        return last ? *last : runtime.Table(1 - from);
    }

    /// The caret left the text the words describe. A layout switch between erase and retype keeps `_erased`.
    void _drop(const bool switching = false) {
        _word.Clear();
        _run.Clear();
        _heldIntact = false;
        if (!switching) {
            _erased.Count = 0;
        }
    }

    /// The judged word erased whole and its keys typed again from the other side: the user's answer.
    bool _retyped(const int from) {
        if (!_erasedWhole || !std::ranges::equal(std::span(_erased.Keys.data(), _erased.Count),
                                                 std::span(_word.Keys.data(), _word.Count), {},
                                                 &Convert::Key::Vk, &Convert::Key::Vk)) {
            return false;
        }
        const int side = _sideOf(*_runtime, _erased.Layout);
        if (side < 0 || side == from) {
            return false;
        }
        Input::Post([runtime = _runtime, erased = _erased, side] { Autocorrect::Overruled(*runtime, erased, side); });
        return true;
    }

    void _autoResult(Input::HoldId id, const std::shared_ptr<const Runtime>& runtime,
                     const Convert::Verdict& verdict, uint64_t at, HKL layout);

    Judgement _judgement(const Convert::Verdict& verdict) {
        return verdict.WrongLayout || verdict.Fixed.empty() ? Judgement::None
               : verdict.Undecided ? Judgement::Undecided : Judgement::Kept;
    }

    /// Autocorrect's to judge: not converted already, and not the rest of a word a mid-word hold converts.
    bool _judging() {
        return _runtime->Auto && _word.Count && !_word.Spaces && !_word.Layout && !(_heldId && _heldEarly);
    }

    /// The word's keys changed: a new version for the judge.
    void _typed() {
        ++_gen;
        if (_judging()) {
            Judge::Typed(_word, _gen);
        }
    }

    /// Holds the typing while the lane settles the word: a fix, or no verdict yet. `ready` needs only the password
    /// check; `early`: the word goes on.
    void _decideHeld(const int from, const HKL layout, const HWND focus, const Convert::Verdict* ready,
                     const bool early = false) {
        const Input::HoldId before = _heldId;
        Input::Edit([word = _word, runtime = _runtime, from, layout, focus, at = GetTickCount64(),
                     ready = ready ? std::optional(*ready) : std::nullopt] {
            const Input::HoldId id = Input::CurrentHold();
            Convert::Verdict verdict = ready ? *ready
                : Autocorrect::Decide(*runtime, *Learning::Current(), {word.Keys.data(), word.Count}, from);
            if (verdict.WrongLayout && Autocorrect::Guarded(*runtime, focus)) {
                verdict = {};
            }
            Autocorrect::Log(*runtime, verdict);
            Input::Post([id, runtime, verdict = std::move(verdict), at, layout] {
                _autoResult(id, runtime, verdict, at, layout);
            });
        });
        if (_heldId == before) {
            Autocorrect::LogSkip(*_runtime, "a hold is still up", 0);
            return;
        }
        _autoSpaces = !early;
        _heldEarly = early;
        _held.EarlyAt = early ? _held.Count : 0;
    }

    /// The judge is sure mid-word: the word so far converts now, and the rest is typed in the new layout.
    void _onEarly(const uint32_t gen, const HKL layout, const Convert::Verdict& verdict) {
        // Typed on since, tried once, or erased and retyped: its Space hears the user out
        if (!_runtime || gen != _gen || _heldId || !_judging() || _word.EarlyAt || _erasedWhole
            || _word.Window != GetForegroundWindow()) {
            return;
        }
        const HWND focus = InputLanguage::FocusedWindow();
        const int from = focus && InputLanguage::LayoutOf(focus) == layout ? _sideOf(*_runtime, layout) : -1;
        if (from >= 0) {
            _decideHeld(from, layout, focus, &verdict, true);
        }
    }

    Input::Verdict _onKey(const Input::KeyEvent& event) {
        if (const uint8_t bit = WordEdit::ModifierBit(event.Vk)) {
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

        const HWND window = GetForegroundWindow();
        const bool allowed = _apps.Allowed(_runtime.get(), window);
        if (window != _word.Window || !allowed) {
            _drop();
            _word.Window = window;
            _undo = {};
        }
        if (!allowed) {
            return Input::Verdict::Next;
        }
        const bool chord = _modifiers & WordEdit::ChordBits;
        if (event.Vk == VK_SPACE && !chord && _heldId && (_autoSpaces || _heldEarly)
            && _held.Window == window && !_word.Count) {
            if (_autoSpaces < MaxSpaces) {
                ++_autoSpaces;
            } else {
                _typedSince = true;
            }
            return Input::Verdict::Next;
        }
        // An undo lasts through the fixed word's spaces, and the rest of a word fixed mid-word
        const bool onFixed = !chord && _word.Count
            && (event.Vk == VK_SPACE ? _word.Spaces < MaxSpaces : TypingKeys[event.Vk] && !_word.Spaces);
        if (_undo.Original && !onFixed) {
            _undo = {};
        }
        _typedSince = true;
        if (event.Vk == VK_BACK && !chord) {
            if (_word.Spaces) {
                --_word.Spaces;
            } else if (_word.Count) {
                if (_word.Judged != Judgement::None) {
                    _erased = _word;
                    _erasedWhole = false;
                }
                _word.Judged = Judgement::None;
                if (!--_word.Count) {
                    // Gone from the screen: the next word is read in the layout it is typed in
                    _word.Clear();
                    _erasedWhole = _erased.Count != 0;
                }
                _typed();
            } else {
                // Into the run's spaces, or text never seen
                _drop();
            }
        } else if (event.Vk == VK_SPACE && !chord) {
            if (_judging() && !event.Repeat) {
                const HWND focus = InputLanguage::FocusedWindow();
                const HKL layout = focus ? _layoutOf(_word, focus) : nullptr;
                const int from = layout ? _sideOf(*_runtime, layout) : -1;
                if (from < 0) {
                    Autocorrect::LogSkip(*_runtime, "layout outside the pair", reinterpret_cast<UINT_PTR>(layout));
                } else if (!_retyped(from)) {
                    // Pinned before the hold snapshots it: the conversion reads the layout typed in
                    _word.Layout = layout;
                    const Convert::Verdict* ready = Judge::Ready(_gen, layout);
                    if (ready && !ready->WrongLayout) {
                        // Kept: nothing to edit, so the typing goes on unheld
                        _word.Judged = _judgement(*ready);
                        Judge::LogKept();
                    } else {
                        _decideHeld(from, layout, focus, ready);
                    }
                }
            }
            _erased.Count = 0;
            if (_word.Count && _word.Spaces < MaxSpaces) {
                ++_word.Spaces;
            } else if (!_word.Count && !_heldId) {
                // Uncounted, it would put the run's erase one short
                _drop();
            }
        } else if (chord || !TypingKeys[event.Vk]) {
            if (event.Vk == VK_SPACE) {
                Autocorrect::LogSkip(*_runtime, "modifiers held", _modifiers);
            }
            // Win+Space switches the layout
            _drop(event.Vk == VK_SPACE);
        } else {
            if (_word.Spaces || _word.Count == MaxKeys) {
                // More spaces than counted may sit on screen: the run would erase the wrong text
                const bool joins = _word.Judged == Judgement::Undecided && _word.Spaces && _word.Spaces < MaxSpaces;
                _run = joins ? TypedWord::Join(_run, _word) : Word{};
                _word.Clear();
                _heldIntact = false;
            }
            if (!_erasedWhole) {
                // Typed into a half-erased word: an edit, no retype
                _erased.Count = 0;
            }
            // A mid-word fix stands for the rest of its word
            if (!_word.EarlyAt || _word.Judged != Judgement::Fixed) {
                _word.Judged = Judgement::None;
            }
            _word.Keys[_word.Count++] = {event.Vk, (_modifiers & WordEdit::ShiftBits) != 0, _caps};
            _typed();
        }
        return Input::Verdict::Next;
    }

    void _reset() {
        _word = {};
        _run = {};
        _heldRun = {};
        _heldIntact = false;
        _heldId = 0;
        _autoSpaces = 0;
        _heldEarly = false;
        _heldFocus = nullptr;
        _undo = {};
        _erased = {};
        _erasedWhole = false;
        _apps = {};
        _modifiers = 0;
        _caps = GetKeyState(VK_CAPITAL) & 1;
        _last = {};
    }

    void _onHold(const Input::HoldId id) {
        _held = _word;
        _heldRun = _run;
        _heldId = id;
        _autoSpaces = 0;
        _heldEarly = false;
        _heldFocus = InputLanguage::FocusedWindow();
        _heldModifiers = _modifiers;
        _typedSince = false;
        _word.Clear();
        _run.Clear();
        _heldIntact = true;
    }

    /// The caret is still where the hold was taken, in an app still tracked.
    bool _onTarget() {
        const HWND focus = InputLanguage::FocusedWindow();
        return focus && focus == _heldFocus && GetForegroundWindow() == _held.Window
               && _apps.Allowed(_runtime.get(), _held.Window);
    }

    /// The held word is `_word` again, unless typing moved on; after a mid-word hold, with what was typed during it.
    bool _rejoin() {
        if (!_typedSince) {
            _word = _held;
            return true;
        }
        if (!_heldEarly || !_heldIntact || _autoSpaces || _held.Count + _word.Count > MaxKeys) {
            return false;
        }
        _word.Layout = _held.Layout;
        _word = TypedWord::Join(_held, _word);
        return true;
    }

    /// No edit went out: the held words are back in the buffer, unless typing or the caret moved on.
    bool _unhold() {
        _heldId = 0;
        const bool rest = _typedSince;
        if (!_onTarget() || !_rejoin()) {
            return false;
        }
        _run = _heldRun;
        if (rest) {
            // The judge has not seen the word with its rest
            _typed();
        }
        return true;
    }

    bool _apply(const Input::HoldId id, const bool autoSpace = false, const HKL requested = nullptr) {
        if (!_runtime || !id || id != _heldId || !_held.Count) {
            return false;
        }
        _heldId = 0;
        if (!_onTarget()) {
            return false;
        }
        const HKL layout = _layoutOf(_held, _heldFocus);
        const int from = _sideOf(*_runtime, layout);
        if (from < 0) {
            return false;
        }
        // A resolved side means the layout is installed, so Find cannot miss
        const Convert::LayoutTable& source = *_runtime->Find(layout);
        const Convert::LayoutTable* target = requested ? _runtime->Find(requested) : &_target(*_runtime, from);
        if (!target) {
            return false;
        }
        const std::span<const Convert::Key> keys{_held.Keys.data(), _held.Count};
        const std::wstring text = target->Render(keys);
        std::wstring typed = source.Render(keys);
        if (text.empty() || typed.empty()) {
            return false;
        }

        const std::vector<INPUT> edit = WordEdit::Replace(_held.Count, _held.Spaces, text, _heldModifiers);

        InputLanguage::SwitchTo(_heldFocus, target->Layout());
        if (!Input::Commit(id, edit)) {
            InputLanguage::SwitchTo(_heldFocus, layout);
            return false;
        }
        LayoutLayer::Requested(target->Layout());
        if (autoSpace) {
            Autocorrect::Announce(std::move(typed), text);
        }
        _undo = {};
        if (!autoSpace && _held.Judged != Judgement::None) {
            Autocorrect::Overruled(*_runtime, _held, from);
        }
        if (autoSpace) {
            // Typed during the hold, so they follow the fixed word
            _held.Spaces = _autoSpaces;
        }
        if (_rejoin()) {
            ++_gen;
            _word.Judged = autoSpace ? Judgement::Fixed : Judgement::None;
            if (autoSpace) {
                _undo = {layout, target->Layout(), _heldFocus, _held.Window};
            }
            _word.Layout = target->Layout();
        }
        return true;
    }

    void _autoResult(const Input::HoldId id, const std::shared_ptr<const Runtime>& runtime,
                     const Convert::Verdict& verdict, const uint64_t at, const HKL layout) {
        if (!id || id != _heldId || runtime != _runtime || !_held.Count) {
            Autocorrect::LogFix(*runtime, verdict, false, at);
            return;
        }
        if (!_onTarget()) {
            _heldId = 0;
            Autocorrect::LogFix(*runtime, verdict, false, at);
            return;
        }
        if (verdict.WrongLayout) {
            const Word word = _held;
            // Pinned only now: a mid-word fix that does not land leaves the word to its Space
            _held.Layout = layout;
            // Only a sure fix carries the undecided words before it, the ngram is a guess
            if (verdict.ByDictionary || verdict.ByRule || verdict.ByUser) {
                _held = TypedWord::Join(_heldRun, _held);
            }
            const bool landed = _apply(id, true);
            Autocorrect::LogFix(*runtime, verdict, landed, at);
            if (landed) {
                if (_held.Count != word.Count) {
                    Autocorrect::LogRun(*runtime, _heldRun);
                }
                return;
            }
            _held = word;
        }
        _held.Spaces = _autoSpaces;
        _held.Judged = _judgement(verdict);
        if (!_unhold() && _typedSince && _heldIntact && _held.Judged == Judgement::Undecided
            && _autoSpaces < MaxSpaces) {
            _run = TypedWord::Join(_heldRun, _held);
        }
        Input::Commit(id, {});
    }

} // anonymous namespace

    void Register() {
        Judge::Register(&_onEarly);
        Input::Add({.Id = StageId, .Order = 300, .OnKey = &_onKey, .OnReset = &_reset, .OnHold = &_onHold});
    }

    void Use(std::shared_ptr<const Runtime> runtime, const bool on) {
        Input::Post([runtime = std::move(runtime)] {
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

    void ConvertWord(const Input::HoldId id) {
        Input::Post([id] {
            _undo = {};
            if (id && id == _heldId && !_apply(id)) {
                _unhold();
            }
        });
    }

    void UndoAutoConvert(const Input::HoldId id) {
        Input::Post([id] {
            if (!id || id != _heldId) {
                return;
            }
            const Undo pending = _undo;
            const bool valid = pending.Original && pending.Corrected == _held.Layout
                && pending.Focus == _heldFocus && pending.Window == _held.Window
                && _held.Count && !_typedSince;
            _undo = {};
            if (valid && _apply(id, false, pending.Original)) {
                return;
            }
            if (_unhold() && valid) {
                _undo = pending;
            }
            Input::Commit(id, {});
        });
    }
}
