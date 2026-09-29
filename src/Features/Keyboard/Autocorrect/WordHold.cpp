#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../LayoutLayer.hpp"
#include "Learning.hpp"
#include "WordEdit.hpp"
#include "WordState.hpp"
#include "WordTracker.hpp"

namespace WordTracker {

namespace {

    using Autocorrect::Runtime;
    using TypedWord::Judgement;
    using TypedWord::MaxKeys;
    using TypedWord::MaxSpaces;
    using TypedWord::Word;

    struct Conversion {
        HKL Layout;
        int From;
        const Convert::LayoutTable* Target;
        std::wstring Text;
        std::wstring Typed;
    };

    bool _onTarget() {
        const HWND focus = InputLanguage::FocusedWindow();
        return focus && focus == _held.Focus && GetForegroundWindow() == _held.Word.Window
               && _apps.Allowed(_runtime.get(), _held.Word.Window);
    }

    /// The held word is `_word` again, unless typing moved on; after a mid-word hold, with what was typed during it.
    bool _rejoin() {
        if (!_held.TypedSince) {
            _word = _held.Word;
            return true;
        }
        if (!_held.Early || !_held.Intact || _held.AutoSpaces || _held.Word.Count + _word.Count > MaxKeys) {
            return false;
        }
        _word.Layout = _held.Word.Layout;
        _word = TypedWord::Join(_held.Word, _word);
        return true;
    }

    /// No edit went out: the held words are back in the buffer, unless typing or the caret moved on.
    bool _unhold() {
        _held.Id = 0;
        const bool rest = _held.TypedSince;
        if (!_onTarget() || !_rejoin()) {
            return false;
        }
        _run = _held.Run;
        if (rest) {
            _wordChanged();
        }
        return true;
    }

    std::optional<Conversion> _prepare(const Input::HoldId id, const HKL requested) {
        if (!_runtime || !id || id != _held.Id || !_held.Word.Count) {
            return std::nullopt;
        }
        _held.Id = 0;
        if (!_onTarget()) {
            return std::nullopt;
        }
        const HKL layout = _layoutOf(_held.Word, _held.Focus);
        const int from = _runtime->SideOf(layout);
        if (from < 0) {
            return std::nullopt;
        }
        // The pair's layout, never a third one: a fix into uk left the user typing there, unjudged
        const Convert::LayoutTable* target = requested ? _runtime->Find(requested) : &_runtime->Table(1 - from);
        if (!target) {
            return std::nullopt;
        }
        const std::span<const Convert::Key> keys{_held.Word.Keys.data(), _held.Word.Count};
        Conversion conversion{layout, from, target, target->Render(keys), _runtime->Find(layout)->Render(keys)};
        // A key that types nothing (a dead key) leaves the Backspace count unknown
        if (conversion.Text.empty() || conversion.Typed.empty()) {
            return std::nullopt;
        }
        return conversion;
    }

    bool _send(const Input::HoldId id, const Conversion& conversion) {
        const std::vector<INPUT> edit =
            WordEdit::Replace(_held.Word.Count, _held.Word.Spaces, conversion.Text, _held.Modifiers);
        InputLanguage::SwitchTo(_held.Focus, conversion.Target->Layout());
        if (!Input::Commit(id, edit)) {
            InputLanguage::SwitchTo(_held.Focus, conversion.Layout);
            return false;
        }
        LayoutLayer::Requested(conversion.Target->Layout());
        _undo = {};
        return true;
    }

    void _rejoined(const Conversion& conversion, const Judgement judged) {
        ++_wordVersion;
        _word.Judged = judged;
        _word.Layout = conversion.Target->Layout();
    }

    bool _fixHeld(const Input::HoldId id) {
        auto conversion = _prepare(id, nullptr);
        if (!conversion || !_send(id, *conversion)) {
            return false;
        }
        Autocorrect::Announce(std::move(conversion->Typed), conversion->Text);
        _held.Word.Spaces = _held.AutoSpaces;
        if (_rejoin()) {
            _rejoined(*conversion, Judgement::Fixed);
            _undo = {conversion->Layout, _held.Focus, _word, _word};
        }
        return true;
    }

    /// `taught`: the word the user's convert or undo overrules, the held one if null.
    bool _convertHeld(const Input::HoldId id, const HKL requested = nullptr, const Word* taught = nullptr) {
        const auto conversion = _prepare(id, requested);
        if (!conversion || !_send(id, *conversion)) {
            return false;
        }
        if (const Word& word = taught ? *taught : _held.Word; word.Judged != Judgement::None) {
            Autocorrect::Overruled(*_runtime, word, conversion->From);
        }
        if (_rejoin()) {
            _rejoined(*conversion, Judgement::None);
        }
        return true;
    }

    void _undoHeld(const Input::HoldId id) {
        const Undo pending = _undo;
        const bool valid = pending.Original && pending.Focus == _held.Focus && !_held.TypedSince;
        _undo = {};
        if (valid) {
            const Word held = std::exchange(_held.Word, pending.Text);
            if (_convertHeld(id, pending.Original, &pending.Fixed)) {
                return;
            }
            _held.Word = held;
        }
        if (_unhold() && valid) {
            _undo = pending;
        }
    }

    bool _landFix(const Input::HoldId id, const Runtime& runtime, const Convert::Verdict& verdict, const uint64_t at,
                  const HKL layout) {
        const Word word = _held.Word;
        // Pinned only now: a mid-word fix that does not land leaves the word to its Space
        _held.Word.Layout = layout;
        // Only a sure fix carries the undecided words before it, the ngram is a guess
        if (verdict.ByDictionary || verdict.ByRule || verdict.ByUser) {
            _held.Word = TypedWord::Join(_held.Run, _held.Word);
        }
        const bool landed = _fixHeld(id);
        Autocorrect::LogFix(runtime, verdict, landed, at);
        if (!landed) {
            _held.Word = word;
            return false;
        }
        if (_held.Word.Count != word.Count) {
            Autocorrect::LogRun(runtime, _held.Run);
        }
        return true;
    }

    void _autoResult(const Input::HoldId id, const std::shared_ptr<const Runtime>& runtime,
                     const Convert::Verdict& verdict, const uint64_t at, const HKL layout) {
        if (!id || id != _held.Id || runtime != _runtime || !_held.Word.Count) {
            Autocorrect::LogFix(*runtime, verdict, false, at);
            return;
        }
        if (!_onTarget()) {
            _held.Id = 0;
            Autocorrect::LogFix(*runtime, verdict, false, at);
            return;
        }
        if (verdict.WrongLayout && _landFix(id, *runtime, verdict, at, layout)) {
            return;
        }
        _held.Word.Spaces = _held.AutoSpaces;
        _held.Word.Judged = _judgement(verdict);
        if (!_unhold() && _held.TypedSince && _held.Intact && _held.Word.Judged == Judgement::Undecided
            && _held.AutoSpaces < MaxSpaces) {
            _run = TypedWord::Join(_held.Run, _held.Word);
        }
    }

    /// Edit lane: the password check, the log, then the verdict goes to the input thread under this hold.
    void _settle(const std::shared_ptr<const Runtime>& runtime, const HWND focus, Convert::Verdict verdict,
                 const uint64_t at, const HKL layout) {
        if (verdict.WrongLayout && Autocorrect::Guarded(*runtime, focus)) {
            verdict = {};
        }
        Autocorrect::Log(*runtime, verdict);
        Input::Post([id = Input::CurrentHold(), runtime, verdict = std::move(verdict), at, layout] {
            _autoResult(id, runtime, verdict, at, layout);
        });
    }

} // anonymous namespace

    void _onHold(const Input::HoldId id) {
        _held = {.Word = _word,
                 .Run = _run,
                 .Id = id,
                 .Focus = InputLanguage::FocusedWindow(),
                 .Modifiers = _modifiers,
                 .Intact = true};
        _word.Clear();
        _run.Clear();
    }

    void _decideHeld(const int from, const HKL layout, const HWND focus, const Convert::Verdict* ready,
                     const bool early) {
        const Input::HoldId before = _held.Id;
        const uint64_t at = GetTickCount64();
        if (ready) {
            Input::Edit([verdict = *ready, runtime = _runtime, focus, at, layout]() mutable {
                _settle(runtime, focus, std::move(verdict), at, layout);
            });
        } else {
            Input::Edit([word = _word, runtime = _runtime, from, focus, at, layout] {
                _settle(runtime, focus,
                        Autocorrect::Decide(*runtime, *Learning::Current(), {word.Keys.data(), word.Count}, from), at,
                        layout);
            });
        }
        if (_held.Id == before) {
            Autocorrect::LogSkip(*_runtime, "a hold is still up", 0);
            return;
        }
        _held.AutoSpaces = !early;
        _held.Early = early;
        _held.Word.EarlyAt = early ? _held.Word.Count : 0;
    }

    void ConvertWord(const Input::HoldId id) {
        Input::Post([id] {
            _undo = {};
            if (id && id == _held.Id && !_convertHeld(id)) {
                _unhold();
            }
        });
    }

    void UndoAutoConvert(const Input::HoldId id) {
        Input::Post([id] {
            if (id && id == _held.Id) {
                _undoHeld(id);
            }
        });
    }
}
