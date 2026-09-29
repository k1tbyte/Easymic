#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "Core/AppConfig.hpp"
#include "Features/Keyboard/Autocorrect/UserRules.hpp"
#include "Platform/Str.hpp"

namespace {

    int _failures = 0;

    void _check(const bool ok, const char* name) {
        if (!ok) {
            std::printf("FAIL: %s\n", name);
            ++_failures;
        }
    }

    WordRule _rule(const char* text, const WordMatch match = WordMatch::Exact, const bool always = true,
                   const bool caseSensitive = false) {
        return {.Text = text, .Match = match, .CaseSensitive = caseSensitive, .Always = always};
    }

    UserRules::Compiled _compile(std::initializer_list<WordRule> rules) {
        LearnedWords words;
        for (const WordRule& rule : rules) UserRules::Put(words, rule);
        return UserRules::Compiled(words);
    }

    void _normalize() {
        WordRule rule = _rule("  ,he[ \t");
        _check(UserRules::Normalize(rule) && rule.Text == ",he[", "outer whitespace trims, punctuation stays");
        for (const char* bad : {"", "   ", "a b", "a\tb"}) {
            rule = _rule(bad);
            _check(!UserRules::Normalize(rule), "empty or inner whitespace is rejected");
        }
        rule = _rule(std::string(64, 'a').c_str());
        _check(UserRules::Normalize(rule), "64 keys fit");
        rule = _rule(std::string(65, 'a').c_str());
        _check(!UserRules::Normalize(rule), "65 keys do not");
        rule = _rule("\xd0\x91\xd1\x80\xd1\x83\xd1\x85");
        _check(UserRules::Normalize(rule) && rule.Text == "\xd0\x91\xd1\x80\xd1\x83\xd1\x85", "Cyrillic keeps its case");
    }

    void _put() {
        LearnedWords words;
        _check(UserRules::Put(words, _rule("Foo")), "a new rule is added");
        _check(!UserRules::Put(words, _rule("Foo")), "the same rule changes nothing");
        _check(UserRules::Put(words, _rule("fOO", WordMatch::Exact, false)) && words.Rules.size() == 1
               && words.Rules[0].Text == "fOO" && !words.Rules[0].Always, "same text ignoring case replaces the action");
        _check(UserRules::Put(words, _rule("foo", WordMatch::Exact, true, true)) && words.Rules.size() == 2,
               "a case-sensitive rule is another rule");
        _check(UserRules::Put(words, _rule("foo", WordMatch::StartsWith)) && words.Rules.size() == 3,
               "another condition is another rule");
        _check(UserRules::Put(words, _rule("bar"), 2) && words.Rules.size() == 3 && words.Rules[2].Text == "bar",
               "an edit replaces its row");
        _check(UserRules::Put(words, _rule("FOO", WordMatch::Exact, true), 2) && words.Rules.size() == 2
               && words.Rules[0].Text == "FOO" && words.Rules[0].Always, "an edit into a duplicate merges the rows");
        _check(!UserRules::Put(words, _rule(" "), 0) && words.Rules.size() == 2, "an invalid edit keeps the row");
    }

    void _answer() {
        const auto rules = _compile({_rule(",he["), _rule("he", WordMatch::Exact, false), _rule("123"), _rule("!!!", WordMatch::Exact, false)});
        _check(rules.Answer(L",he[") == true, ",he[ converts");
        _check(rules.Answer(L"he") == false, "he keeps its own rule");
        _check(rules.Answer(L",HE[") == true, "case-insensitive by default");
        _check(!rules.Answer(L"he[") && !rules.Answer(L",he"), "no trimming of edges");
        _check(rules.Answer(L"123") == true && rules.Answer(L"!!!") == false, "digits and signs only");

        const auto kinds = _compile({_rule("ghb", WordMatch::StartsWith), _rule("Nt", WordMatch::Contains, true, true),
                                     _rule("\xd0\xbf\xd1\x80\xd0\xb8", WordMatch::StartsWith, false)});
        _check(kinds.Answer(L"GHBDTN") == true && !kinds.Answer(L"xghb"), "starts with");
        _check(kinds.Answer(L"cNtk") == true && !kinds.Answer(L"cntk") && !kinds.Answer(L"cNTk"), "contains, match case");
        _check(kinds.Answer(L"ПРИвет") == false, "Cyrillic ignores case");
        _check(!kinds.Answer(L"").has_value(), "empty word");

        const auto conflict = _compile({_rule("collide"), _rule("coll", WordMatch::StartsWith, false)});
        _check(conflict.Answer(L"collide") == false, "never wins over always, whatever the order");
        _check(_compile({}).Answer(L"x") == std::nullopt, "no rules, no answer");
    }

    void _refused() {
        const auto rules = _compile({_rule("hello", WordMatch::Exact, false), _rule("Pre", WordMatch::StartsWith, false, true),
                                     _rule("always")});
        _check(rules.Refused(L"he") && rules.Refused(L"HEL"), "a start of a never word waits");
        _check(!rules.Refused(L"hellox") && !rules.Refused(L"xhe"), "other words do not");
        _check(rules.Refused(L"Pr") && rules.Refused(L"Prefix"), "a never start, before and after it is complete");
        _check(!rules.Refused(L"pre"), "a case-sensitive start ignores other case");
        _check(!rules.Refused(L"alw"), "always rules never hold back");
        const auto contains = _compile({_rule("zz", WordMatch::Contains, false)});
        _check(contains.Refused(L"a") && !contains.Refused(L""), "a never substring holds every word to its Space");
    }

    // The matcher before user rules: trimmed, lowercased, UTF-8, linear
    std::string _oldEntry(const std::wstring_view text) {
        const auto edge = [](const wchar_t c) { return c < 128 && !((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')); };
        size_t b = 0, e = text.size();
        while (b < e && edge(text[b])) ++b;
        while (e > b && edge(text[e - 1])) --e;
        return Str::WideToUtf8(Str::Lower(text.substr(b, e - b)));
    }

    void _bench() {
        using Clock = std::chrono::steady_clock;
        constexpr int Passes = 20000;
        std::puts("rules   old ns   exact ns  ignore-case ns  (per lookup, missing word)");
        for (const size_t count : {0u, 10u, 100u, 1000u}) {
            LearnedWords exact, ignoring;
            std::vector<std::string> old;
            for (size_t i = 0; i < count; ++i) {
                const std::string text = "word" + std::to_string(i);
                exact.Rules.push_back({.Text = text, .CaseSensitive = true});
                ignoring.Rules.push_back({.Text = text});
                old.push_back(text);
            }
            const UserRules::Compiled sensitive(exact), insensitive(ignoring);
            const std::wstring typed = L"Ghbdtn";
            size_t sink = 0;
            const auto time = [&](auto lookup) {
                const auto start = Clock::now();
                for (int i = 0; i < Passes; ++i) sink += lookup();
                return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / Passes;
            };
            const double before = time([&] { return static_cast<size_t>(std::ranges::contains(old, _oldEntry(typed))); });
            const double withCase = time([&] { return static_cast<size_t>(sensitive.Answer(typed).has_value()); });
            const double withoutCase = time([&] { return static_cast<size_t>(insensitive.Answer(typed).has_value()); });
            std::printf("%5zu %9.0f %10.0f %15.0f%s\n", count, before, withCase, withoutCase, sink ? "!" : "");
        }
    }
}

int main(const int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--bench") {
        _bench();
        return 0;
    }
    _normalize();
    _put();
    _answer();
    _refused();
    if (_failures) return 1;
    std::puts("user rules checks passed");
}
