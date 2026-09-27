#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Features/Keyboard/Convert/Detector.hpp"
#include "Features/Keyboard/Convert/Rules.hpp"
#include "Features/Keyboard/TypedWord.hpp"
#include "Platform/Str.hpp"

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
    }

    // The 2026-09-27 log: after a clear, "не" read a stale "en" slot and became "yt"
    void _context() {
        Convert::LanguageContext context;
        for (int i = 0; i < 4; ++i) {
            context.Note("en");
        }
        context.Clear();
        context.Note("ru");
        _check(context.Preference("ru", "en") == -1.0, "a cleared context forgets its old slots");

        context.Typing("en");
        context.Note("en");
        context.Typing("en");
        _check(!context.Empty(), "the same language keeps the context");
        context.Typing("ru");
        _check(context.Empty(), "a switch to another language's layout restarts it");

        context.Note("ru");
        context.Switched("en");
        context.Typing("en");
        _check(context.Preference("en", "ru") == -1.0, "a fix restarts the context with its language");
    }
}

int main() {
    _rules();
    _join();
    _context();
    if (_failures) {
        return 1;
    }
    std::puts("autocorrect checks passed");
}
