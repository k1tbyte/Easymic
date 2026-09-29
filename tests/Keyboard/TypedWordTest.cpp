#include <cstdio>

#include "../Check.hpp"
#include "TypedWordFixture.hpp"

namespace {

    using Test::Check;
    using TypedWord::Erased;
    using TypedWord::Judgement;
    using TypedWord::Word;

    const HKL Us = reinterpret_cast<HKL>(0x04090409);
    const HKL Ru = reinterpret_cast<HKL>(0x04190419);

    Word _judged(const char* keys, const Judgement judged, const HKL layout = Us) {
        Word word = Fixture::Word(keys, 0, layout);
        word.Judged = judged;
        return word;
    }

    bool _typed(Erased& erased, Word& word, const char* keys) {
        for (; *keys; ++keys) {
            TypedWord::Append(word, erased, {static_cast<uint8_t>(*keys), false, false});
        }
        return erased.Whole;
    }

    void _join() {
        const Word joined = TypedWord::Join(Fixture::Word("YE", 2, Us), Fixture::Word("UKZYE", 1, Us));
        Check(joined.Count == 9 && joined.Keys[2].Vk == VK_SPACE && joined.Keys[3].Vk == VK_SPACE
                  && joined.Keys[4].Vk == 'U' && joined.Spaces == 1 && joined.Layout == Us,
              "run, spaces, word");
        Check(TypedWord::Join({}, Fixture::Word("UKZYE", 1, Us)).Count == 5, "an empty run");
        Check(TypedWord::Join(Fixture::Word("YE", 1, Ru), Fixture::Word("UKZYE", 1, Us)).Count == 5, "another layout");
        Word full = Fixture::Word("", 1, Us);
        full.Count = 60;
        Check(TypedWord::Join(full, Fixture::Word("UKZYE", 1, Us)).Count == 5, "overflow keeps the word");
    }

    void _type() {
        Word text = Fixture::Word("YE", 1, Us);
        const auto type = [&](const uint8_t vk) { return TypedWord::Type(text, {vk, false, false}); };
        Check(type('U') && text.Count == 4 && text.Keys[2].Vk == VK_SPACE && !text.Spaces, "a space turns into a key");
        Check(type(VK_SPACE) && type(VK_BACK) && type(VK_BACK) && text.Count == 3 && !text.Spaces, "backspace");
        Check(!type(VK_LEFT) && !type(VK_RETURN), "a caret move ends the text");
        Check(type(VK_BACK) && type(VK_BACK) && !type(VK_BACK), "erased whole");

        Word gone;
        Check(!TypedWord::Type(gone, {VK_BACK, false, false}) && !gone.Count && !gone.Spaces,
              "backspace on nothing neither wraps nor continues");
        Word spaced = Fixture::Word("", TypedWord::MaxSpaces, Us);
        Check(!TypedWord::Type(spaced, {VK_SPACE, false, false}) && spaced.Spaces == TypedWord::MaxSpaces,
              "spaces stop at the limit");
    }

    void _erase() {
        Erased erased;
        Word word = _judged("GHBDTN", Judgement::Fixed);
        word.Layout = Ru;
        for (int i = 0; i < 5; ++i) {
            TypedWord::EraseKey(word, erased);
        }
        Check(word.Count == 1 && word.Judged == Judgement::None && !erased.Whole && erased.Text.Count == 6,
              "the first erase keeps the judged word, and it is not gone yet");
        TypedWord::EraseKey(word, erased);
        Check(!word.Count && !word.Layout && erased.Whole && erased.Text.Judged == Judgement::Fixed,
              "erased whole: the word is forgotten, the erased one remembered");

        Check(_typed(erased, word, "GHBDT") && word.Count == 5, "the same keys again stay a retype");
        _typed(erased, word, "X");
        Check(!erased.Whole && !erased.Text.Count, "another key ends the retype");

        Word unjudged = Fixture::Word("AB", 0, Us);
        Erased untouched;
        TypedWord::EraseKey(unjudged, untouched);
        TypedWord::EraseKey(unjudged, untouched);
        Check(!untouched.Whole && !untouched.Text.Count, "an unjudged word teaches nothing");

        Erased half;
        Word partly = _judged("AB", Judgement::Kept);
        TypedWord::EraseKey(partly, half);
        _typed(half, partly, "B");
        Check(!half.Whole && !half.Text.Count, "a half-erased word is no retype");
    }

    void _reopen() {
        Word kept = _judged("AB", Judgement::Kept);
        Erased erased;
        TypedWord::Append(kept, erased, {'C', false, false});
        Check(kept.Count == 3 && !kept.Layout && kept.Judged == Judgement::None, "typing on after a keep reads anew");

        Word fixed = _judged("AB", Judgement::Fixed, Ru);
        fixed.EarlyAt = 2;
        TypedWord::Append(fixed, erased, {'C', false, false});
        Check(fixed.Layout == Ru && fixed.Judged == Judgement::Fixed, "a mid-word fix stands for the rest of its word");

        Word pinned = _judged("AB", Judgement::Fixed, Ru);
        TypedWord::Append(pinned, erased, {'C', false, false});
        Check(pinned.Layout == Ru && pinned.Judged == Judgement::None, "a fix on the Space is over once typed on");
    }

    void _rollover() {
        Word run;
        Word word = _judged("YE", Judgement::Undecided);
        Check(!TypedWord::Rollover(word, run) && word.Count == 2 && !run.Count, "no spaces, no new word");

        word.Spaces = 1;
        Check(TypedWord::Rollover(word, run) && !word.Count && run.Count == 2 && run.Spaces == 1,
              "an undecided word joins the run");

        Word next = _judged("UK", Judgement::Undecided);
        next.Spaces = 1;
        Check(TypedWord::Rollover(next, run) && run.Count == 5 && run.Keys[2].Vk == VK_SPACE, "and the run grows");

        Word kept = _judged("OK", Judgement::Kept);
        kept.Spaces = 1;
        Check(TypedWord::Rollover(kept, run) && !run.Count, "a decided word ends the run");

        Word saturated = _judged("YE", Judgement::Undecided);
        saturated.Spaces = TypedWord::MaxSpaces;
        Check(TypedWord::Rollover(saturated, run) && !run.Count, "spaces at the limit may hide more: no run");

        Word full = Fixture::Word("", 0, Us);
        full.Count = TypedWord::MaxKeys;
        Check(TypedWord::Rollover(full, run) && !full.Count, "a full word starts the next one");
    }
}

int main() {
    _join();
    _type();
    _erase();
    _reopen();
    _rollover();
    if (Test::Failures) {
        return 1;
    }
    std::puts("typed word checks passed");
}
