#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Features/Keyboard/Convert/Detector.hpp"
#include "Features/Keyboard/Convert/Rules.hpp"
#include "Features/Keyboard/Autocorrect/TypedWord.hpp"
#include "Platform/Str.hpp"
#include "langpack/PackBuilder.hpp"

namespace {

    int _failures = 0;

    void _check(const bool ok, const char* what) {
        if (!ok) {
            std::printf("FAILED: %s\n", what);
            ++_failures;
        }
    }

    TypedWord::Word _word(const char* keys, const uint8_t spaces, const HKL layout) {
        TypedWord::Word word{.Spaces = spaces, .Layout = layout};
        for (; *keys; ++keys) {
            word.Keys[word.Count++] = {static_cast<uint8_t>(*keys), false, false};
        }
        return word;
    }

    void _rules() {
        const auto path = std::filesystem::temp_directory_path() / L"easylauncher-rules-test.rules";
        std::ofstream(path, std::ios::binary) << "\xEF\xBB\xBF" << Str::WideToUtf8(
            L"PSVersion=20170627\n_PD a\n_P ye\n_B ofc\nyfd\n_EP yfdf\n_CP NE\n_B by'n \r\n"
            L"_A юу \n_P ф\n");
        Convert::Rules rules;
        _check(rules.Load(path) && rules.Count() == 8, "load skips the header and D rules");
        std::filesystem::remove(path);

        const auto find = [&](const std::wstring& word, const bool cyrillic = false) {
            return rules.Find(word, cyrillic).Pattern;
        };
        _check(find(L"ye") == L"P ye" && find(L"Ye") == L"P ye", "whole word, any case");
        _check(find(L"yes").empty(), "a whole-word rule does not match a longer word");
        _check(find(L"ofcrf") == L"B ofc" && find(L"xofc").empty(), "word begin only at the begin");
        _check(find(L"yfdthyjt") == L"A yfd" && rules.Find(L"yfdthyjt", false).Anywhere, "anywhere");
        _check(find(L"yfdf").empty(), "an exception cancels the switch");
        _check(find(L"NE") == L"P NE" && find(L"ne").empty(), "case-sensitive");
        _check(find(L"by'n") == L"B by'n " && find(L"by'nf").empty(), "a trailing space anchors the end");
        _check(find(L"ююу", true) == L"A юу ", "anywhere at the word end");
        _check(find(L"юую", true).empty(), "an end-anchored pattern mid-word");
        _check(find(L"ф", true) == L"P ф" && find(L"ф").empty(), "scripts stay apart");
        _check(find(L"a").empty(), "D rules are skipped");

        const auto open = [&](const std::wstring& word, const bool cyrillic = false) {
            return rules.Find(word, cyrillic, true).Pattern;
        };
        _check(open(L"ofcr") == L"B ofc" && open(L"yfdt") == L"A yfd", "begin and anywhere mid-word");
        _check(open(L"ye").empty() && open(L"by'n").empty() && open(L"ююу", true).empty(),
               "whole-word and end-anchored patterns wait for the end");
        _check(open(L"yfdf").empty(), "a whole-word exception holds mid-word");
    }

    void _join() {
        const HKL us = reinterpret_cast<HKL>(0x04090409), ru = reinterpret_cast<HKL>(0x04190419);
        const TypedWord::Word joined = TypedWord::Join(_word("YE", 2, us), _word("UKZYE", 1, us));
        _check(joined.Count == 9 && joined.Keys[2].Vk == VK_SPACE && joined.Keys[3].Vk == VK_SPACE
               && joined.Keys[4].Vk == 'U' && joined.Spaces == 1 && joined.Layout == us, "run, spaces, word");
        _check(TypedWord::Join({}, _word("UKZYE", 1, us)).Count == 5, "an empty run");
        _check(TypedWord::Join(_word("YE", 1, ru), _word("UKZYE", 1, us)).Count == 5, "another layout");
        TypedWord::Word full = _word("", 1, us);
        full.Count = 60;
        _check(TypedWord::Join(full, _word("UKZYE", 1, us)).Count == 5, "overflow keeps the word");

        TypedWord::Word text = _word("YE", 1, us);
        const auto type = [&](const uint8_t vk) { return TypedWord::Type(text, {vk, false, false}); };
        _check(type('U') && text.Count == 4 && text.Keys[2].Vk == VK_SPACE && !text.Spaces, "a space turns into a key");
        _check(type(VK_SPACE) && type(VK_BACK) && type(VK_BACK) && text.Count == 3 && !text.Spaces, "backspace");
        _check(!type(VK_LEFT) && !type(VK_RETURN), "a caret move ends the text");
        _check(type(VK_BACK) && type(VK_BACK) && !type(VK_BACK), "erased whole");
    }

    /// Built as langpack builds one, each word ten times so its letters make the ngram alphabet.
    bool _pack(const std::filesystem::path& path, const char* locale, const std::wstring& words) {
        const auto list = std::filesystem::path(path).replace_extension(L".txt");
        {
            std::ofstream out(list, std::ios::binary);
            for (int i = 0; i < 10; ++i) {
                out << Str::WideToUtf8(words);
            }
        }
        const bool built = BuildPack(list.string(), {.Locale = locale}, path.string(), nullptr);
        std::filesystem::remove(list);
        return built;
    }

    /// Words that give `target` every trigram without being a word themselves: a plausible unknown word.
    std::wstring _around(const std::wstring& target, const wchar_t pad = L'и') {
        return target + pad + L'\n' + pad + target + L'\n';
    }

    void _detect() {
        HKL installed[32]{};
        const int count = GetKeyboardLayoutList(32, installed);
        const auto layout = [&](const WORD language) -> HKL {
            for (int i = 0; i < count; ++i) {
                if (LOWORD(reinterpret_cast<UINT_PTR>(installed[i])) == language) return installed[i];
            }
            return nullptr;
        };
        if (!layout(0x0409) || !layout(0x0419)) {
            std::puts("detection skipped: needs the en-US and ru-RU layouts");
            return;
        }

        const auto temp = std::filesystem::temp_directory_path();
        const auto enPath = temp / L"easylauncher-test-en.pack", ruPath = temp / L"easylauncher-test-ru.pack";
        const auto rulesPath = temp / L"easylauncher-test-detect.rules";
        std::ofstream(rulesPath, std::ios::binary) << "\xEF\xBB\xBF_P hello\n_B vbh\n_P vs\n_P b\n_B hellp\n_B lff\n_P fff\n"
                                                   << Str::WideToUtf8(L"_B хат\n");
        {
            Convert::Pack enPack, ruPack;
            Convert::Rules rules;
            _check(_pack(enPath, "en", L"hello\nvs\nworld\n" + _around(L"hellp", L'a'))
                   && _pack(ruPath, "ru", L"привет\nмы\nмир\nи\nз\nда\nкасса\n" + _around(L"даа") + _around(L"руддз")
                                              + _around(L"мировой"))
                   && enPack.Load(enPath.wstring()) && ruPack.Load(ruPath.wstring()) && rules.Load(rulesPath),
                   "packs and rules load");
            const Convert::LayoutTable enTable(layout(0x0409)), ruTable(layout(0x0419));
            const Convert::Side en{&enTable, &enPack}, ru{&ruTable, &ruPack};
            const auto detect = [&](const char* keys, const Convert::Side& typed, const Convert::Side& other,
                                    const bool frequency = true) {
                const TypedWord::Word word = _word(keys, 0, nullptr);
                return Convert::Detect({word.Keys.data(), word.Count}, typed, other, 0, &rules, frequency);
            };
            const auto early = [&](const char* keys, const bool frequency) {
                const TypedWord::Word word = _word(keys, 0, nullptr);
                return Convert::Early({word.Keys.data(), word.Count}, en, ru, &rules, frequency).WrongLayout;
            };

            const Convert::Verdict fixed = detect("GHBDTN", en, ru);
            _check(fixed.WrongLayout && fixed.ByDictionary && fixed.Fixed == L"привет", "a word of the other side");
            _check(detect("HELLO", ru, en).Fixed == L"hello" && detect("HELLO", ru, en).WrongLayout, "and back");
            _check(!detect("HELLO", en, ru).WrongLayout, "a known word stays, a rule on it or not");
            const auto both = detect("VS", en, ru);
            _check(!both.WrongLayout && both.Undecided, "a rule cannot override a word of both dictionaries");
            _check(!detect("VS", en, ru, false).WrongLayout, "both dictionaries also guard rules without frequency");
            const auto letter = detect("B", en, ru);
            _check(!letter.WrongLayout && letter.Undecided, "one letter waits for the next word");
            _check(detect("LFF", en, ru).ByRule && !detect("LFFF", en, ru).WrongLayout, "an elongation stays");
            auto capital = _word("FFF", 0, nullptr);
            capital.Keys[0].Shift = true;
            _check(!Convert::Detect({capital.Keys.data(), capital.Count}, en, ru, 0, &rules).WrongLayout,
                   "a capitalized elongation stays");
            _check(!detect("P", en, ru, false).WrongLayout, "one letter never switches on the dictionary alone");
            _check(detect("B", en, ru, false).ByRule, "explicit rule-only mode can switch one letter");
            _check(detect("LF", en, ru).WrongLayout, "two-letter dictionary corrections still work");
            for (const bool frequency : {false, true}) {
                _check(!detect("144P", en, ru, frequency).WrongLayout, "digits cannot be trimmed into dictionary hits");
                _check(!detect("GHBDTN123", en, ru, frequency).WrongLayout, "a numeric identifier stays at the boundary");
                auto flag = _word("-B", 0, nullptr);
                flag.Keys[0].Vk = VK_OEM_MINUS;
                _check(!Convert::Detect({flag.Keys.data(), flag.Count}, en, ru, 0, &rules, frequency).WrongLayout,
                       "a command flag is not a dictionary word");
            }
            const auto typo = detect("HELLP", en, ru);
            _check(typo.Margin() < 0 && !typo.WrongLayout, "a long begin rule cannot overrule a negative margin");
            _check(detect("HELLP", en, ru, false).ByRule, "rule-only mode retains targeted overrides");
            _check(detect("VBHJDJQ", en, ru, false).ByRule, "a rule converts unknown words without frequency");
            const auto detectWord = [&](const TypedWord::Word& typedWord, const Convert::Side& from,
                                        const Convert::Side& to, const bool frequency) {
                return Convert::Detect({typedWord.Keys.data(), typedWord.Count}, from, to, 0, &rules, frequency);
            };
            const Convert::Verdict unseen = detect("LFFP", en, ru, false);
            _check(detect("LFF", en, ru, false).ByRule && !unseen.WrongLayout && unseen.Implausible,
                   "a rule cannot fix into text with a trigram no word has");
            auto sign = _word("XFNF", 0, nullptr);
            sign.Keys[0].Vk = VK_OEM_4;
            _check(!detectWord(sign, ru, en, false).WrongLayout && detectWord(sign, ru, en, false).Implausible,
                   "a sign where a letter was typed is no word of the other side");
            auto closing = _word("HELLO?", 0, nullptr);
            closing.Keys[5].Vk = VK_OEM_COMMA;
            _check(detectWord(closing, ru, en, true).ByDictionary, "a closing comma is punctuation");
            _check(early("LFF", false) && !early("LFFP", false), "mid-word: the start must be plausible too");
            _check(early("GHBD", true) && !early("GHB", true), "mid-word: a sure start from the 4th key");
            _check(early("VBH", false) && !early("GHBD", false), "mid-word without frequency: a rule alone");
            _check(!early("HELL", true), "mid-word: the start of a word of its own language");
            _check(!early("RFCC", true) && early("RFCCF", true), "mid-word: a doubled letter waits a key");
        }
        for (const auto& path : {enPath, ruPath, rulesPath}) {
            std::filesystem::remove(path);
        }
    }
}

int main() {
    _rules();
    _join();
    _detect();
    if (_failures) {
        return 1;
    }
    std::puts("autocorrect checks passed");
}
