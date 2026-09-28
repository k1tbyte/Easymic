#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Features/Keyboard/Convert/Detector.hpp"
#include "Features/Keyboard/Convert/Rules.hpp"
#include "Features/Keyboard/TypedWord.hpp"
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
        std::ofstream(rulesPath, std::ios::binary) << "\xEF\xBB\xBF_P hello\n_B vbh\n";
        {
            Convert::Pack enPack, ruPack;
            Convert::Rules rules;
            _check(_pack(enPath, "en", L"hello\nvs\nworld\n") && _pack(ruPath, "ru", L"привет\nмы\nмир\n")
                   && enPack.Load(enPath.wstring()) && ruPack.Load(ruPath.wstring()) && rules.Load(rulesPath),
                   "packs and rules load");
            const Convert::LayoutTable enTable(layout(0x0409)), ruTable(layout(0x0419));
            const Convert::Side en{&enTable, &enPack}, ru{&ruTable, &ruPack};
            const auto detect = [&](const char* keys, const Convert::Side& typed, const Convert::Side& other) {
                const TypedWord::Word word = _word(keys, 0, nullptr);
                return Convert::Detect({word.Keys.data(), word.Count}, typed, other, 0, &rules);
            };
            const auto early = [&](const char* keys, const bool frequency) {
                const TypedWord::Word word = _word(keys, 0, nullptr);
                return Convert::Early({word.Keys.data(), word.Count}, en, ru, &rules, frequency).WrongLayout;
            };

            const Convert::Verdict fixed = detect("GHBDTN", en, ru);
            _check(fixed.WrongLayout && fixed.ByDictionary && fixed.Fixed == L"привет", "a word of the other side");
            _check(detect("HELLO", ru, en).Fixed == L"hello" && detect("HELLO", ru, en).WrongLayout, "and back");
            _check(!detect("HELLO", en, ru).WrongLayout, "a known word stays, a rule on it or not");
            _check(detect("VS", en, ru).Undecided, "a short word of both languages waits for the next");
            _check(detect("VBHJDJQ", en, ru).ByRule, "a rule converts what no dictionary knows");
            _check(early("GHBD", true) && !early("GHB", true), "mid-word: a sure start from the 4th key");
            _check(early("VBH", false) && !early("GHBD", false), "mid-word without frequency: a rule alone");
            _check(!early("HELL", true), "mid-word: the start of a word of its own language");
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
