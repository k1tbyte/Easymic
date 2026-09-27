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
        std::wstring Rule;
        std::string_view SourceLocale;
        std::string_view FixedLocale;

        double Margin() const { return ScoreFixed - ScoreOriginal; }
        const char* Reason() const {
            return ByUser ? "user" : ByRule ? "rule" : ByDictionary ? "dict" : Ambiguous ? "ambiguous" : "ngram";
        }
    };

    /// `frequency` off: Punto and a dictionary - rules unguarded but for known words, then the dictionary, no ngram.
    Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
                   double thresholdOverride = 0, const Rules* rules = nullptr, bool frequency = true);

}
