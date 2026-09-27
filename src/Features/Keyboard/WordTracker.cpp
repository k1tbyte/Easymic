#include "WordTracker.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/AppConfig.hpp"
#include "Autocorrect.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Lifecycle.hpp"
#include "InputLanguage.hpp"
#include "Learning.hpp"
#include "LayoutLayer.hpp"
#include "TypedWord.hpp"
#include "WordEdit.hpp"
#include "Platform/Foreground.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

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

    // UI thread
    KeyboardSettings* _settings = nullptr;
    bool _needed = false;

    // Input thread
    std::shared_ptr<const Runtime> _runtime;
    enum class AppStatus : uint8_t { Unknown, Allowed, Excluded };
    HWND _appWindow = nullptr;
    AppStatus _appStatus = AppStatus::Unknown;
    Word _word;
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
    Undo _undo;
    /// A judged word being erased: its keys typed again from the other side teach.
    Word _erased;
    bool _erasedWhole = false;
    uint8_t _modifiers = 0;
    bool _caps = false;
    /// The layout each pair side was last typed in: a conversion goes there, not to the pair's.
    std::array<HKL, 2> _last{};

    bool _allowed(const HWND window) {
        if (!_runtime || !window) {
            return false;
        }
        if (!_runtime->Auto && _runtime->Excluded.empty() && !_runtime->SkipFullscreen) {
            return true;
        }
        if (window != _appWindow) {
            _appWindow = window;
            _appStatus = AppStatus::Unknown;
        }
        if (_appStatus == AppStatus::Unknown) {
            const auto foreground = Foreground::Current();
            if (foreground && foreground->Window == window && !foreground->Exe.empty()) {
                _appStatus = _runtime->Excluded.contains(foreground->Exe)
                             || (_runtime->SkipFullscreen && foreground->Fullscreen)
                    ? AppStatus::Excluded : AppStatus::Allowed;
            }
        }
        return _appStatus == AppStatus::Allowed;
    }

    HKL _layoutOf(const Word& word, const HWND focus) {
        return word.Layout ? word.Layout : InputLanguage::LayoutOf(focus);
    }

    /// The pair side a layout types for, itself or by script, noted as that side's last. -1: none.
    int _sideOf(const Runtime& runtime, const HKL layout) {
        int side = runtime.Table(0).Layout() == layout ? 0 : runtime.Table(1).Layout() == layout ? 1 : -1;
        if (const Convert::LayoutTable* table = runtime.Find(layout); side < 0 && table) {
            const bool a = table->Script() == runtime.Table(0).Script();
            const bool b = table->Script() == runtime.Table(1).Script();
            side = a == b ? -1 : a ? 0 : 1;
        }
        if (side >= 0) {
            _last[side] = layout;
        }
        return side;
    }

    const Convert::LayoutTable& _target(const Runtime& runtime, const int from) {
        const Convert::LayoutTable* last = runtime.Find(_last[1 - from]);
        return last ? *last : runtime.Table(1 - from);
    }

    /// LogDecisions only, and off the input thread: the hook must not wait on the log file.
    void _skip(const char* why, const uint64_t detail) {
        if (_runtime->LogDecisions) {
            Dispatcher::Post([why, detail] {
                Logger::Log(Logger::Level::Info, "Keyboard: Space skipped, %s (%llx)", why, detail);
            });
        }
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

    /// The user overruled autocorrect on `word`, on screen on pair side `side`: a fix teaches never, a keep always.
    void _overruled(const Word& word, const int side) {
        const std::span<const Convert::Key> keys{word.Keys.data(), word.Count};
        // A phrase: which of its words erred is unknown
        if (!_runtime || std::ranges::contains(keys, VK_SPACE, &Convert::Key::Vk)) {
            return;
        }
        const bool always = word.Judged != Judgement::Fixed;
        // Taught as typed, and a fix is on screen on the other side
        Learning::Teach(_runtime->Table(always ? side : 1 - side).Render(keys), always);
    }

    void _logRun(const Word& run) {
        const Convert::LayoutTable* table = _runtime->Find(run.Layout);
        if (_runtime->LogDecisions && table) {
            Dispatcher::Post([text = Str::WideToUtf8(table->Render({run.Keys.data(), run.Count}))] {
                Logger::Log(Logger::Level::Info, "Keyboard: FIX %s too, the next word decided", text.c_str());
            });
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
        Input::Post([erased = _erased, side] { _overruled(erased, side); });
        return true;
    }

    void _autoResult(Input::HoldId id, const std::shared_ptr<const Runtime>& runtime,
                     const Convert::Verdict& verdict, uint64_t spaceAt);

    /// A fix held the typing from its Space until it landed or was dropped (a hold that timed out drops it).
    void _logFix(const Runtime& runtime, const Convert::Verdict& verdict, const bool landed, const uint64_t spaceAt) {
        if (runtime.LogDecisions && verdict.WrongLayout) {
            Dispatcher::Post([landed, ms = GetTickCount64() - spaceAt] {
                Logger::Log(Logger::Level::Info, "Keyboard: fix %s after %llu ms", landed ? "landed" : "dropped", ms);
            });
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
        const bool allowed = _allowed(window);
        if (window != _word.Window || !allowed) {
            _drop();
            _word.Window = window;
            _undo = {};
        }
        if (!allowed) {
            return Input::Verdict::Next;
        }
        const bool chord = _modifiers & WordEdit::ChordBits;
        if (event.Vk == VK_SPACE && !chord && _heldId && _autoSpaces
            && _held.Window == window && !_word.Count) {
            if (_autoSpaces < MaxSpaces) {
                ++_autoSpaces;
            } else {
                _typedSince = true;
            }
            return Input::Verdict::Next;
        }
        if (_undo.Original && (event.Vk != VK_SPACE || chord || !_word.Count
                             || !_word.Spaces || _word.Spaces == MaxSpaces)) {
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
            } else {
                // Into the run's spaces, or text never seen
                _drop();
            }
        } else if (event.Vk == VK_SPACE && !chord) {
            if (_word.Count && !_word.Spaces && _runtime->Auto && !event.Repeat) {
                const HWND focus = InputLanguage::FocusedWindow();
                const HKL layout = focus ? _layoutOf(_word, focus) : nullptr;
                const int from = layout ? _sideOf(*_runtime, layout) : -1;
                if (from < 0) {
                    _skip("layout outside the pair", reinterpret_cast<UINT_PTR>(layout));
                } else if (!_retyped(from)) {
                    // Pinned before the hold snapshots it: the conversion reads the layout typed in
                    _word.Layout = layout;
                    const Input::HoldId before = _heldId;
                    Input::Edit([word = _word, runtime = _runtime, learned = Learning::Current(), from, focus,
                                 spaceAt = GetTickCount64()] {
                        const Input::HoldId id = Input::CurrentHold();
                        const Convert::Verdict verdict = Autocorrect::Decide(
                            *runtime, *learned, {word.Keys.data(), word.Count}, from, focus);
                        Input::Post([id, runtime, verdict, spaceAt] { _autoResult(id, runtime, verdict, spaceAt); });
                    });
                    if (_heldId != before) {
                        _autoSpaces = 1;
                    } else {
                        _skip("a hold is still up", 0);
                    }
                }
            }
            _erased.Count = 0;
            if (_word.Count && _word.Spaces < MaxSpaces) {
                ++_word.Spaces;
            }
        } else if (chord || !TypingKeys[event.Vk]) {
            if (event.Vk == VK_SPACE) {
                _skip("modifiers held", _modifiers);
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
            _word.Judged = Judgement::None;
            _word.Keys[_word.Count++] = {event.Vk, (_modifiers & WordEdit::ShiftBits) != 0, _caps};
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
        _heldFocus = nullptr;
        _undo = {};
        _erased = {};
        _erasedWhole = false;
        _appWindow = nullptr;
        _appStatus = AppStatus::Unknown;
        _modifiers = 0;
        _caps = GetKeyState(VK_CAPITAL) & 1;
        _last = {};
    }

    void _onHold(const Input::HoldId id) {
        _held = _word;
        _heldRun = _run;
        _heldId = id;
        _autoSpaces = 0;
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
        return focus && focus == _heldFocus && GetForegroundWindow() == _held.Window && _allowed(_held.Window);
    }

    /// No edit went out: the held words are back in the buffer, unless typing or the caret moved on.
    bool _unhold() {
        _heldId = 0;
        if (_typedSince || !_onTarget()) {
            return false;
        }
        _word = _held;
        _run = _heldRun;
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
            _overruled(_held, from);
        }
        if (!_typedSince) {
            _word = _held;
            _word.Judged = autoSpace ? Judgement::Fixed : Judgement::None;
            if (autoSpace) {
                _word.Spaces = _autoSpaces;
                _undo = {layout, target->Layout(), _heldFocus, _held.Window};
            }
            _word.Layout = target->Layout();
        }
        return true;
    }

    void _autoResult(const Input::HoldId id, const std::shared_ptr<const Runtime>& runtime,
                     const Convert::Verdict& verdict, const uint64_t spaceAt) {
        if (!id || id != _heldId || runtime != _runtime || !_held.Count) {
            _logFix(*runtime, verdict, false, spaceAt);
            return;
        }
        if (!_onTarget()) {
            _heldId = 0;
            _logFix(*runtime, verdict, false, spaceAt);
            return;
        }
        if (verdict.WrongLayout) {
            const Word word = _held;
            // Only a sure fix carries the undecided words before it, the ngram is a guess
            if (verdict.ByDictionary || verdict.ByRule || verdict.ByUser) {
                _held = TypedWord::Join(_heldRun, _held);
            }
            const bool landed = _apply(id, true);
            _logFix(*runtime, verdict, landed, spaceAt);
            if (landed) {
                if (_held.Count != word.Count) {
                    _logRun(_heldRun);
                }
                return;
            }
            _held = word;
        }
        _held.Spaces = _autoSpaces;
        _held.Judged = verdict.WrongLayout || verdict.Fixed.empty() ? Judgement::None
                       : verdict.Undecided ? Judgement::Undecided : Judgement::Kept;
        if (!_unhold() && _typedSince && _heldIntact && _held.Judged == Judgement::Undecided
            && _autoSpaces < MaxSpaces) {
            _run = TypedWord::Join(_heldRun, _held);
        }
        Input::Commit(id, {});
    }

    void _restore() {
        if (!_needed && !_settings->AutoCorrect) {
            return;
        }
        auto runtime = Autocorrect::Resolve(*_settings);
        if (!runtime) {
            LOG_WARNING("Keyboard: the conversion pair is not two installed layouts");
            return;
        }
        const bool enable = _needed || runtime->Auto;
        Input::Post([runtime = std::move(runtime)] {
            _runtime = std::move(runtime);
            _undo = {};
        });
        if (enable) {
            Input::Enable(StageId, Input::WantKeys | Input::WantButtons);
        }
    }

    void _suspend() {
        _needed = false;
        Input::Disable(StageId);
        Input::Post([] {
            _runtime.reset();
            _reset();
        });
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
                && _held.Count && _held.Spaces && !_typedSince;
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
