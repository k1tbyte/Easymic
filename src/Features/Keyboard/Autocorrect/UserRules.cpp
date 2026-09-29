#include "UserRules.hpp"

#include <algorithm>
#include <functional>

#include "Core/AppConfig.hpp"
#include "Platform/Str.hpp"
#include "TypedWord.hpp"

namespace UserRules {

namespace {
    std::wstring _key(const WordRule& rule) {
        const std::wstring text = Str::Utf8ToWide(rule.Text);
        return rule.CaseSensitive ? text : Str::Lower(text);
    }
}

    bool Normalize(WordRule& rule) {
        constexpr std::string_view whitespace = " \t\r\n\f\v";
        const size_t start = rule.Text.find_first_not_of(whitespace);
        if (start == std::string::npos) return false;
        rule.Text = rule.Text.substr(start, rule.Text.find_last_not_of(whitespace) - start + 1);
        const std::wstring text = Str::Utf8ToWide(rule.Text);
        return !text.empty() && text.size() <= TypedWord::MaxKeys
               && std::ranges::none_of(text, [](wchar_t c) { return c <= L' ' || c == L'\x7f'; });
    }

    bool Put(LearnedWords& words, WordRule rule, const size_t replacing) {
        if (!Normalize(rule)) return false;
        const std::wstring key = _key(rule);
        for (size_t i = 0; i < words.Rules.size(); ++i) {
            const WordRule& existing = words.Rules[i];
            if (i == replacing || existing.Match != rule.Match || existing.CaseSensitive != rule.CaseSensitive
                || _key(existing) != key) continue;
            const bool changed = existing != rule;
            words.Rules[i] = std::move(rule);
            if (replacing < words.Rules.size()) {
                words.Rules.erase(words.Rules.begin() + replacing);
                return true;
            }
            return changed;
        }
        if (replacing < words.Rules.size()) {
            if (words.Rules[replacing] == rule) return false;
            words.Rules[replacing] = std::move(rule);
        } else {
            words.Rules.push_back(std::move(rule));
        }
        return true;
    }

    Compiled::Compiled(const LearnedWords& words) {
        for (WordRule rule : words.Rules) {
            if (!Normalize(rule)) continue;
            if (rule.Match == WordMatch::Exact) {
                _words[rule.Always][rule.CaseSensitive].push_back(_key(rule));
            } else {
                _patterns.push_back({_key(rule), rule.Match == WordMatch::Contains, rule.CaseSensitive, rule.Always});
                _containsNever |= !rule.Always && rule.Match == WordMatch::Contains;
            }
        }
        for (auto& byCase : _words) {
            for (auto& sorted : byCase) std::ranges::sort(sorted);
        }
    }

    std::optional<bool> Compiled::Answer(const std::wstring_view text) const {
        if (text.empty()) return std::nullopt;
        std::wstring lower;
        const auto typed = [&](const bool caseSensitive) -> std::wstring_view {
            if (!caseSensitive && lower.empty()) lower = Str::Lower(text);
            return caseSensitive ? text : lower;
        };
        for (const bool always : {false, true}) {
            for (const bool caseSensitive : {false, true}) {
                const auto& sorted = _words[always][caseSensitive];
                if (!sorted.empty() && std::ranges::binary_search(sorted, typed(caseSensitive), std::less<std::wstring_view>{})) {
                    return always;
                }
            }
            for (const Pattern& pattern : _patterns) {
                const std::wstring_view word = pattern.Always == always ? typed(pattern.CaseSensitive) : std::wstring_view{};
                if (!word.empty() && (pattern.Contains ? word.contains(pattern.Text) : word.starts_with(pattern.Text))) {
                    return always;
                }
            }
        }
        return std::nullopt;
    }

    bool Compiled::Refused(const std::wstring_view text) const {
        if (text.empty()) return false;
        // Any later key can still form a refused substring
        if (_containsNever) return true;
        std::wstring lower;
        const auto typed = [&](const bool caseSensitive) -> std::wstring_view {
            if (!caseSensitive && lower.empty()) lower = Str::Lower(text);
            return caseSensitive ? text : lower;
        };
        for (const bool caseSensitive : {false, true}) {
            const auto& sorted = _words[false][caseSensitive];
            if (sorted.empty()) continue;
            // Words that start with the text sort right at its lower bound
            const std::wstring_view word = typed(caseSensitive);
            const auto it = std::ranges::lower_bound(sorted, word, std::less<std::wstring_view>{});
            if (it != sorted.end() && it->starts_with(word)) return true;
        }
        return std::ranges::any_of(_patterns, [&](const Pattern& pattern) {
            const std::wstring_view word = pattern.Always ? std::wstring_view{} : typed(pattern.CaseSensitive);
            return !word.empty() && (pattern.Text.starts_with(word) || word.starts_with(pattern.Text));
        });
    }
}
