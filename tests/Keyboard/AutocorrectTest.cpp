#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "../Check.hpp"
#include "Features/Keyboard/Convert/Detector.hpp"
#include "Features/Keyboard/Convert/Rules.hpp"
#include "Platform/Str.hpp"
#include "TypedWordFixture.hpp"
#include "langpack/PackBuilder.hpp"

namespace {

    using Test::Check;
    using Fixture::Word;

    void _rules() {
        const auto path = std::filesystem::temp_directory_path() / L"easylauncher-rules-test.rules";
        std::ofstream(path, std::ios::binary) << "\xEF\xBB\xBF" << Str::WideToUtf8(
            L"PSVersion=20170627\n_PD a\n_P ye\n_B ofc\nyfd\n_EP yfdf\n_CP NE\n_B by'n \r\n"
            L"_A юу \n_P ф\n");
        Convert::Rules rules;
        Check(rules.Load(path) && rules.Count() == 8, "load skips the header and D rules");
        std::filesystem::remove(path);

        const auto find = [&](const std::wstring& word, const bool cyrillic = false) {
            return rules.Find(word, cyrillic).Pattern;
        };
        Check(find(L"ye") == L"P ye" && find(L"Ye") == L"P ye", "whole word, any case");
        Check(find(L"yes").empty(), "a whole-word rule does not match a longer word");
        Check(find(L"ofcrf") == L"B ofc" && find(L"xofc").empty(), "word begin only at the begin");
        Check(find(L"yfdthyjt") == L"A yfd" && rules.Find(L"yfdthyjt", false).Anywhere, "anywhere");
        Check(find(L"yfdf").empty(), "an exception cancels the switch");
        Check(find(L"NE") == L"P NE" && find(L"ne").empty(), "case-sensitive");
        Check(find(L"by'n") == L"B by'n " && find(L"by'nf").empty(), "a trailing space anchors the end");
        Check(find(L"ююу", true) == L"A юу ", "anywhere at the word end");
        Check(find(L"юую", true).empty(), "an end-anchored pattern mid-word");
        Check(find(L"ф", true) == L"P ф" && find(L"ф").empty(), "scripts stay apart");
        Check(find(L"a").empty(), "D rules are skipped");

        const auto open = [&](const std::wstring& word, const bool cyrillic = false) {
            return rules.Find(word, cyrillic, true).Pattern;
        };
        Check(open(L"ofcr") == L"B ofc" && open(L"yfdt") == L"A yfd", "begin and anywhere mid-word");
        Check(open(L"ye").empty() && open(L"by'n").empty() && open(L"ююу", true).empty(),
              "whole-word and end-anchored patterns wait for the end");
        Check(open(L"yfdf").empty(), "a whole-word exception holds mid-word");
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
            Check(false, "detection needs the en-US and ru-RU layouts installed");
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
            Check(_pack(enPath, "en", L"hello\nvs\nworld\n" + _around(L"hellp", L'a'))
                  && _pack(ruPath, "ru", L"привет\nмы\nмир\nи\nз\nда\nкасса\n" + _around(L"даа") + _around(L"руддз")
                                             + _around(L"мировой"))
                  && enPack.Load(enPath.wstring()) && ruPack.Load(ruPath.wstring()) && rules.Load(rulesPath),
                  "packs and rules load");
            const Convert::LayoutTable enTable(layout(0x0409)), ruTable(layout(0x0419));
            const Convert::Side en{&enTable, &enPack}, ru{&ruTable, &ruPack};
            const auto detect = [&](const char* keys, const Convert::Side& typed, const Convert::Side& other,
                                    const bool frequency = true) {
                const auto word = Word(keys);
                return Convert::Detect({word.Keys.data(), word.Count}, typed, other, 0, &rules, frequency);
            };
            const auto early = [&](const char* keys, const bool frequency) {
                const auto word = Word(keys);
                return Convert::Early({word.Keys.data(), word.Count}, en, ru, &rules, frequency).WrongLayout;
            };

            const Convert::Verdict fixed = detect("GHBDTN", en, ru);
            Check(fixed.WrongLayout && fixed.ByDictionary && fixed.Fixed == L"привет", "a word of the other side");
            Check(detect("HELLO", ru, en).Fixed == L"hello" && detect("HELLO", ru, en).WrongLayout, "and back");
            Check(!detect("HELLO", en, ru).WrongLayout, "a known word stays, a rule on it or not");
            const auto both = detect("VS", en, ru);
            Check(!both.WrongLayout && both.Undecided, "a rule cannot override a word of both dictionaries");
            Check(!detect("VS", en, ru, false).WrongLayout, "both dictionaries also guard rules without frequency");
            const auto letter = detect("B", en, ru);
            Check(!letter.WrongLayout && letter.Undecided, "one letter waits for the next word");
            Check(detect("LFF", en, ru).ByRule && !detect("LFFF", en, ru).WrongLayout, "an elongation stays");
            auto capital = Word("FFF");
            capital.Keys[0].Shift = true;
            Check(!Convert::Detect({capital.Keys.data(), capital.Count}, en, ru, 0, &rules).WrongLayout,
                  "a capitalized elongation stays");
            Check(!detect("P", en, ru, false).WrongLayout, "one letter never switches on the dictionary alone");
            Check(detect("B", en, ru, false).ByRule, "explicit rule-only mode can switch one letter");
            Check(detect("LF", en, ru).WrongLayout, "two-letter dictionary corrections still work");
            for (const bool frequency : {false, true}) {
                Check(!detect("144P", en, ru, frequency).WrongLayout, "digits cannot be trimmed into dictionary hits");
                Check(!detect("GHBDTN123", en, ru, frequency).WrongLayout, "a numeric identifier stays at the boundary");
                auto flag = Word("-B");
                flag.Keys[0].Vk = VK_OEM_MINUS;
                Check(!Convert::Detect({flag.Keys.data(), flag.Count}, en, ru, 0, &rules, frequency).WrongLayout,
                      "a command flag is not a dictionary word");
            }
            const auto typo = detect("HELLP", en, ru);
            Check(typo.Margin() < 0 && !typo.WrongLayout, "a long begin rule cannot overrule a negative margin");
            Check(detect("HELLP", en, ru, false).ByRule, "rule-only mode retains targeted overrides");
            Check(detect("VBHJDJQ", en, ru, false).ByRule, "a rule converts unknown words without frequency");
            const auto detectWord = [&](const TypedWord::Word& typedWord, const Convert::Side& from,
                                        const Convert::Side& to, const bool frequency) {
                return Convert::Detect({typedWord.Keys.data(), typedWord.Count}, from, to, 0, &rules, frequency);
            };
            const Convert::Verdict unseen = detect("LFFP", en, ru, false);
            Check(detect("LFF", en, ru, false).ByRule && !unseen.WrongLayout && unseen.Implausible,
                  "a rule cannot fix into text with a trigram no word has");
            auto sign = Word("XFNF");
            sign.Keys[0].Vk = VK_OEM_4;
            Check(!detectWord(sign, ru, en, false).WrongLayout && detectWord(sign, ru, en, false).Implausible,
                  "a sign where a letter was typed is no word of the other side");
            auto closing = Word("HELLO?");
            closing.Keys[5].Vk = VK_OEM_COMMA;
            Check(detectWord(closing, ru, en, true).ByDictionary, "a closing comma is punctuation");
            Check(early("LFF", false) && !early("LFFP", false), "mid-word: the start must be plausible too");
            Check(early("GHBD", true) && !early("GHB", true), "mid-word: a sure start from the 4th key");
            Check(early("VBH", false) && !early("GHBD", false), "mid-word without frequency: a rule alone");
            Check(!early("HELL", true), "mid-word: the start of a word of its own language");
            Check(!early("RFCC", true) && early("RFCCF", true), "mid-word: a doubled letter waits a key");
        }
        for (const auto& path : {enPath, ruPath, rulesPath}) {
            std::filesystem::remove(path);
        }
    }
}

int main() {
    _rules();
    _detect();
    if (Test::Failures) {
        return 1;
    }
    std::puts("autocorrect checks passed");
}
