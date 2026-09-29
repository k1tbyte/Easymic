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
        bool Ambiguous = false;
        bool ByRule = false;
        bool ByUser = false;
        /// A word of both languages: the next word decides.
        bool Undecided = false;
        bool Early = false;
        /// A fix was refused: the result cannot be a word of the other side.
        bool Implausible = false;
        std::wstring Rule;
        std::string_view SourceLocale;
        std::string_view FixedLocale;

        double Margin() const { return ScoreFixed - ScoreOriginal; }
        const char* Reason() const {
            if (ByUser) {
                return "user";
            }
            if (ByRule) {
                return "rule";
            }
            if (ByDictionary) {
                return "dict";
            }
            if (Implausible) {
                return "implausible";
            }
            return Ambiguous ? "ambiguous" : "ngram";
        }
    };

    /// `frequency` off: rules and dictionaries, still preserving known words, flags and numeric tokens.
    Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
                   double thresholdOverride = 0, const Rules* rules = nullptr, bool frequency = true);

    /// Mid-word: `word` so far left the typed language within `StartLetters` letters. `frequency` off: a rule alone decides.
    Verdict Early(std::span<const Key> word, const Side& typed, const Side& other, const Rules* rules,
                  bool frequency = true);

}
