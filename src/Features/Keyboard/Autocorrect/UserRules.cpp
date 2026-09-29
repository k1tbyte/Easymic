#include "UserRules.hpp"

#include <algorithm>
#include <functional>

#include "Core/AppConfig.hpp"
#include "Platform/Str.hpp"
#include "TypedWord.hpp"

namespace UserRules {

namespace {

    using Less = std::less<std::wstring_view>;

    std::wstring _fold(std::wstring text, const bool caseSensitive) {
        return caseSensitive ? text : Str::Lower(text);
    }

    std::wstring _key(const WordRule& rule) {
        return _fold(Str::Utf8ToWide(rule.Text), rule.CaseSensitive);
    }

    /// Trims the rule; its text as wide chars when it is one valid token, else empty.
    std::wstring _token(WordRule& rule) {
        constexpr std::string_view whitespace = " \t\r\n\f\v";
        const size_t start = rule.Text.find_first_not_of(whitespace);
        if (start == std::string::npos) {
            return {};
        }
        rule.Text = rule.Text.substr(start, rule.Text.find_last_not_of(whitespace) - start + 1);
        std::wstring text = Str::Utf8ToWide(rule.Text);
        const bool valid = text.size() <= TypedWord::MaxKeys
                           && std::ranges::none_of(text, [](wchar_t c) { return c <= L' ' || c == L'\x7f'; });
        return valid ? text : std::wstring{};
    }

    /// The typed word as a rule compares it; lowercased once, and only when a rule asks.
    class Typed {
    public:
        explicit Typed(const std::wstring_view text) : _text(text) {}

        std::wstring_view As(const bool caseSensitive) {
            if (caseSensitive) {
                return _text;
            }
            if (_lower.empty()) {
                _lower = Str::Lower(_text);
            }
            return _lower;
        }

    private:
        std::wstring_view _text;
        std::wstring _lower;
    };
}

    bool Normalize(WordRule& rule) {
        return !_token(rule).empty();
    }

    bool Put(LearnedWords& words, WordRule rule, const size_t replacing) {
        if (!Normalize(rule)) {
            return false;
        }
        const std::wstring key = _key(rule);
        for (size_t i = 0; i < words.Rules.size(); ++i) {
            const WordRule& existing = words.Rules[i];
            if (i == replacing || existing.Match != rule.Match || existing.CaseSensitive != rule.CaseSensitive
                || _key(existing) != key) {
                continue;
            }
            const bool changed = existing != rule;
            words.Rules[i] = std::move(rule);
            if (replacing < words.Rules.size()) {
                words.Rules.erase(words.Rules.begin() + replacing);
                return true;
            }
            return changed;
        }
        if (replacing < words.Rules.size()) {
            if (words.Rules[replacing] == rule) {
                return false;
            }
            words.Rules[replacing] = std::move(rule);
        } else {
            words.Rules.push_back(std::move(rule));
        }
        return true;
    }

    Compiled::Compiled(const LearnedWords& words) {
        for (WordRule rule : words.Rules) {
            std::wstring text = _token(rule);
            if (text.empty()) {
                continue;
            }
            text = _fold(std::move(text), rule.CaseSensitive);
            if (rule.Match == WordMatch::Exact) {
                _words[rule.Always][rule.CaseSensitive].push_back(std::move(text));
                continue;
            }
            _containsNever |= !rule.Always && rule.Match == WordMatch::Contains;
            _patterns[rule.Always].push_back({std::move(text), rule.Match == WordMatch::Contains, rule.CaseSensitive});
        }
        for (auto& byCase : _words) {
            for (auto& sorted : byCase) {
                std::ranges::sort(sorted);
            }
        }
    }

    std::optional<bool> Compiled::Answer(const std::wstring_view text) const {
        if (text.empty()) {
            return std::nullopt;
        }
        Typed typed(text);
        const auto hit = [&](const bool always) {
            for (const bool caseSensitive : {false, true}) {
                const auto& sorted = _words[always][caseSensitive];
                if (!sorted.empty() && std::ranges::binary_search(sorted, typed.As(caseSensitive), Less{})) {
                    return true;
                }
            }
            return std::ranges::any_of(_patterns[always], [&](const Pattern& pattern) {
                const std::wstring_view word = typed.As(pattern.CaseSensitive);
                return pattern.Contains ? word.contains(pattern.Text) : word.starts_with(pattern.Text);
            });
        };
        for (const bool always : {false, true}) {
            if (hit(always)) {
                return always;
            }
        }
        return std::nullopt;
    }

    bool Compiled::Refused(const std::wstring_view text) const {
        if (text.empty()) {
            return false;
        }
        if (_containsNever) {
            return true;
        }
        Typed typed(text);
        for (const bool caseSensitive : {false, true}) {
            const auto& sorted = _words[false][caseSensitive];
            if (sorted.empty()) {
                continue;
            }
            const std::wstring_view word = typed.As(caseSensitive);
            const auto it = std::ranges::lower_bound(sorted, word, Less{});
            if (it != sorted.end() && it->starts_with(word)) {
                return true;
            }
        }
        return std::ranges::any_of(_patterns[false], [&](const Pattern& pattern) {
            const std::wstring_view word = typed.As(pattern.CaseSensitive);
            return pattern.Text.starts_with(word) || word.starts_with(pattern.Text);
        });
    }
}
