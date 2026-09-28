#pragma once

#include "LayoutTable.hpp"
#include "Pack.hpp"
#include "Rules.hpp"

#include <span>
#include <string>
#include <string_view>

namespace Convert {

    struct Side {
        const LayoutTable* Table;
        const Pack* Pack;
    };

    struct Verdict {
        bool WrongLayout = false;
        std::wstring Typed;
        std::wstring Fixed;
        double ScoreOriginal = 0.0;
        double ScoreFixed = 0.0;
        bool ByDictionary = false;
        /// Too short to tell, or a word of both languages: kept.
        bool Ambiguous = false;
        bool ByRule = false;
        /// The user taught this word: over every other signal.
        bool ByUser = false;
        /// A word of both languages: the next word decides.
        bool Undecided = false;
        /// On the word so far, mid-word.
        bool Early = false;
        /// A fix was refused: the other side's rules call the result impossible, or it holds signs for letters.
        bool Implausible = false;
        std::wstring Rule;
        std::string_view SourceLocale;
        std::string_view FixedLocale;

        double Margin() const { return ScoreFixed - ScoreOriginal; }
        const char* Reason() const {
            return ByUser ? "user" : ByRule ? "rule" : ByDictionary ? "dict" : Implausible ? "implausible"
                   : Ambiguous ? "ambiguous" : "ngram";
        }
    };

    /// `frequency` off: rules and dictionaries, still preserving known words, flags and numeric tokens.
    Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
                   double thresholdOverride = 0, const Rules* rules = nullptr, bool frequency = true);

    /// Mid-word: `word` so far is in the wrong layout whatever follows, as it left the typed language within
    /// `StartLetters` letters. `frequency` off: a rule alone decides.
    Verdict Early(std::span<const Key> word, const Side& typed, const Side& other, const Rules* rules,
                  bool frequency = true);

}
