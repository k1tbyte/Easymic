#pragma once

#include <memory>

#include "Features/Keyboard/Layout/InputLanguage.hpp"
#include "AppFilter.hpp"
#include "Core/Input/Input.hpp"
#include "Features/Keyboard/Autocorrect/Judge.hpp"
#include "TypedWord.hpp"

/// Input-thread state shared by WordTracker.cpp (keys) and WordHold.cpp (holds).
namespace WordTracker {

    /// The last auto-fix, and all typed after it (`Text`): undo converts the whole text back, the word teaches.
    struct Undo {
        HKL Original = nullptr;
        HWND Focus = nullptr;
        TypedWord::Word Fixed;
        TypedWord::Word Text;
    };

    /// What a hold took from `_word` and `_run`, which start empty and collect what is typed meanwhile.
    struct Held {
        TypedWord::Word Word;
        TypedWord::Word Run;
        Input::HoldId Id = 0;
        HWND Focus = nullptr;
        uint8_t Modifiers = 0;
        uint8_t AutoSpaces = 0;
        /// Only the next word was typed since the hold: the held word still sits right before `_word`.
        bool Intact = false;
        bool TypedSince = false;
        /// The hold converts the word mid-word: keys typed during it are its rest, or its spaces.
        bool Early = false;
    };

    inline std::shared_ptr<const Autocorrect::Runtime> _runtime;
    inline Autocorrect::AppFilter _apps;
    inline TypedWord::Word _word;
    /// The version of the word's keys the judge was told: a verdict on another version is stale.
    inline uint32_t _wordVersion = 0;
    /// Undecided words right before `_word`, joined: a sure fix of the next word converts them too.
    inline TypedWord::Word _run;
    inline Held _held;
    inline Undo _undo;
    inline TypedWord::Erased _erased;
    inline uint8_t _modifiers = 0;
    inline bool _caps = false;

    inline HKL _layoutOf(const TypedWord::Word& word, const HWND focus) {
        return word.Layout ? word.Layout : InputLanguage::LayoutOf(focus);
    }

    inline TypedWord::Judgement _judgement(const Convert::Verdict& verdict) {
        using TypedWord::Judgement;
        return verdict.WrongLayout || verdict.Fixed.empty() ? Judgement::None
               : verdict.Undecided ? Judgement::Undecided : Judgement::Kept;
    }

    /// Autocorrect's to judge: not converted already, and not the rest of a word a mid-word hold converts.
    inline bool _judging() {
        return _runtime->Auto && _word.Count && !_word.Spaces && !_word.Layout && !(_held.Id && _held.Early);
    }

    inline void _wordChanged() {
        ++_wordVersion;
        if (_judging()) {
            Judge::Typed(_word, _wordVersion);
        }
    }

    void _onHold(Input::HoldId id);
    void _decideHeld(int from, HKL layout, HWND focus, const Convert::Verdict* ready, bool early = false);
}
