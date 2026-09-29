#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct LearnedWords;
struct WordRule;
enum class WordMatch;

namespace UserRules {

    /// Trims outer whitespace; one token of up to TypedWord::MaxKeys, signs kept (`,he[` is брух).
    bool Normalize(WordRule& rule);
    /// Adds or updates by text, condition and case; `replacing` is the row being edited. False when nothing changed.
    bool Put(LearnedWords& words, WordRule rule, size_t replacing = static_cast<size_t>(-1));

    /// Immutable snapshot for the judge: whole words binary-searched, never before always.
    class Compiled {
    public:
        Compiled() = default;
        explicit Compiled(const LearnedWords& words);

        /// The whole typed word: convert (true), never (false), or no rule.
        std::optional<bool> Answer(std::wstring_view text) const;
        /// The word so far may still hit a never rule: no mid-word switch.
        bool Refused(std::wstring_view text) const;

    private:
        struct Pattern {
            std::wstring Text;
            bool Contains;
            bool CaseSensitive;
            bool Always;
        };
        /// [always][case-sensitive], sorted.
        std::vector<std::wstring> _words[2][2];
        std::vector<Pattern> _patterns;
        bool _containsNever = false;
    };
}
