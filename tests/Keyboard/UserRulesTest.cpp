#include <cstdio>
#include <string>

#include "../Check.hpp"
#include "Core/AppConfig.hpp"
#include "Features/Keyboard/Autocorrect/UserRules.hpp"

namespace {

    using Test::Check;

    WordRule _rule(const char* text, const WordMatch match = WordMatch::Exact, const bool always = true,
                   const bool caseSensitive = false) {
        return {.Text = text, .Match = match, .CaseSensitive = caseSensitive, .Always = always};
    }

    UserRules::Compiled _compile(std::initializer_list<WordRule> rules) {
        LearnedWords words;
        for (const WordRule& rule : rules) {
            UserRules::Put(words, rule);
        }
        return UserRules::Compiled(words);
    }

    void _normalize() {
        WordRule rule = _rule("  ,he[ \t");
        Check(UserRules::Normalize(rule) && rule.Text == ",he[", "outer whitespace trims, punctuation stays");
        for (const char* bad : {"", "   ", "a b", "a\tb"}) {
            rule = _rule(bad);
            Check(!UserRules::Normalize(rule), "empty or inner whitespace is rejected");
        }
        rule = _rule(std::string(64, 'a').c_str());
        Check(UserRules::Normalize(rule), "64 keys fit");
        rule = _rule(std::string(65, 'a').c_str());
        Check(!UserRules::Normalize(rule), "65 keys do not");
        rule = _rule("\xd0\x91\xd1\x80\xd1\x83\xd1\x85");
        Check(UserRules::Normalize(rule) && rule.Text == "\xd0\x91\xd1\x80\xd1\x83\xd1\x85", "Cyrillic keeps its case");
    }

    void _put() {
        LearnedWords words;
        Check(UserRules::Put(words, _rule("Foo")), "a new rule is added");
        Check(!UserRules::Put(words, _rule("Foo")), "the same rule changes nothing");
        Check(UserRules::Put(words, _rule("fOO", WordMatch::Exact, false)) && words.Rules.size() == 1
              && words.Rules[0].Text == "fOO" && !words.Rules[0].Always, "same text ignoring case replaces the action");
        Check(UserRules::Put(words, _rule("foo", WordMatch::Exact, true, true)) && words.Rules.size() == 2,
              "a case-sensitive rule is another rule");
        Check(UserRules::Put(words, _rule("foo", WordMatch::StartsWith)) && words.Rules.size() == 3,
              "another condition is another rule");
        Check(UserRules::Put(words, _rule("bar"), 2) && words.Rules.size() == 3 && words.Rules[2].Text == "bar",
              "an edit replaces its row");
        Check(UserRules::Put(words, _rule("FOO", WordMatch::Exact, true), 2) && words.Rules.size() == 2
              && words.Rules[0].Text == "FOO" && words.Rules[0].Always, "an edit into a duplicate merges the rows");
        Check(!UserRules::Put(words, _rule(" "), 0) && words.Rules.size() == 2, "an invalid edit keeps the row");
    }

    void _answer() {
        const auto rules = _compile({_rule(",he["), _rule("he", WordMatch::Exact, false), _rule("123"), _rule("!!!", WordMatch::Exact, false)});
        Check(rules.Answer(L",he[") == true, ",he[ converts");
        Check(rules.Answer(L"he") == false, "he keeps its own rule");
        Check(rules.Answer(L",HE[") == true, "case-insensitive by default");
        Check(!rules.Answer(L"he[") && !rules.Answer(L",he"), "no trimming of edges");
        Check(rules.Answer(L"123") == true && rules.Answer(L"!!!") == false, "digits and signs only");

        const auto kinds = _compile({_rule("ghb", WordMatch::StartsWith), _rule("Nt", WordMatch::Contains, true, true),
                                     _rule("\xd0\xbf\xd1\x80\xd0\xb8", WordMatch::StartsWith, false)});
        Check(kinds.Answer(L"GHBDTN") == true && !kinds.Answer(L"xghb"), "starts with");
        Check(kinds.Answer(L"cNtk") == true && !kinds.Answer(L"cntk") && !kinds.Answer(L"cNTk"), "contains, match case");
        Check(kinds.Answer(L"ПРИвет") == false, "Cyrillic ignores case");
        Check(!kinds.Answer(L"").has_value(), "empty word");

        const auto conflict = _compile({_rule("collide"), _rule("coll", WordMatch::StartsWith, false)});
        Check(conflict.Answer(L"collide") == false, "never wins over always, whatever the order");
        const auto reverse = _compile({_rule("coll", WordMatch::StartsWith), _rule("collide", WordMatch::Exact, false)});
        Check(reverse.Answer(L"collide") == false && reverse.Answer(L"collar") == true, "a never word beats an always start");
        Check(_compile({}).Answer(L"x") == std::nullopt, "no rules, no answer");
    }

    void _refused() {
        const auto rules = _compile({_rule("hello", WordMatch::Exact, false), _rule("Pre", WordMatch::StartsWith, false, true),
                                     _rule("always")});
        Check(rules.Refused(L"he") && rules.Refused(L"HEL"), "a start of a never word waits");
        Check(!rules.Refused(L"hellox") && !rules.Refused(L"xhe"), "other words do not");
        Check(rules.Refused(L"Pr") && rules.Refused(L"Prefix"), "a never start, before and after it is complete");
        Check(!rules.Refused(L"pre"), "a case-sensitive start ignores other case");
        Check(!rules.Refused(L"alw"), "always rules never hold back");
        const auto contains = _compile({_rule("zz", WordMatch::Contains, false)});
        Check(contains.Refused(L"a") && !contains.Refused(L""), "a never substring holds every word to its Space");
    }
}

int main() {
    _normalize();
    _put();
    _answer();
    _refused();
    if (Test::Failures) {
        return 1;
    }
    std::puts("user rules checks passed");
}
