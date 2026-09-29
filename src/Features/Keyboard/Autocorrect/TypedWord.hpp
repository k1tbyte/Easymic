#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "../Convert/LayoutTable.hpp"

namespace TypedWord {

    inline constexpr size_t MaxKeys = 64;
    inline constexpr uint8_t MaxSpaces = 8;

    inline constexpr std::array<bool, 256> TypingKeys = [] {
        std::array<bool, 256> keys{};
        for (int vk = '0'; vk <= '9'; ++vk) {
            keys[vk] = true;
        }
        for (int vk = 'A'; vk <= 'Z'; ++vk) {
            keys[vk] = true;
        }
        for (int vk = VK_OEM_1; vk <= VK_OEM_3; ++vk) {
            keys[vk] = true;
        }
        for (int vk = VK_OEM_4; vk <= VK_OEM_8; ++vk) {
            keys[vk] = true;
        }
        keys[VK_OEM_102] = true;
        return keys;
    }();

    /// What autocorrect made of the word on its Space, or mid-word: the user's next move on it teaches.
    enum class Judgement : uint8_t { None, Kept, Undecided, Fixed };

    struct Word {
        std::array<Convert::Key, MaxKeys> Keys{};
        uint8_t Count = 0;
        uint8_t Spaces = 0;
        /// The layout it is on screen in; null until a Space or a conversion pins it.
        HKL Layout = nullptr;
        HWND Window = nullptr;
        Judgement Judged = Judgement::None;
        /// The keys a mid-word fix was tried on (0: none): once per word, and overruling it refuses that start.
        uint8_t EarlyAt = 0;

        void Clear() {
            Count = 0;
            Spaces = 0;
            Layout = nullptr;
            Judged = Judgement::None;
            EarlyAt = 0;
        }
    };

    /// A judged word being erased: its keys typed again from the other side teach.
    struct Erased {
        Word Text;
        bool Whole = false;
    };

    inline void FoldSpaces(Word& word) {
        std::fill_n(word.Keys.begin() + word.Count, word.Spaces, Convert::Key{VK_SPACE, false, false});
        word.Count += word.Spaces;
        word.Spaces = 0;
    }

    /// Typing on after `word`, into one text. False for any other key, or once the text is gone or does not fit.
    inline bool Type(Word& word, const Convert::Key key) {
        if (key.Vk == VK_BACK) {
            if (word.Spaces) {
                --word.Spaces;
            } else if (word.Count) {
                --word.Count;
            } else {
                return false;
            }
            return word.Count != 0;
        }
        if (key.Vk == VK_SPACE) {
            if (word.Spaces >= MaxSpaces) {
                return false;
            }
            ++word.Spaces;
            return true;
        }
        if (!TypingKeys[key.Vk] || word.Count + word.Spaces >= MaxKeys) {
            return false;
        }
        FoldSpaces(word);
        word.Keys[word.Count++] = key;
        return true;
    }

    inline Word Join(const Word& run, const Word& word) {
        if (!run.Count || run.Layout != word.Layout || run.Count + run.Spaces + word.Count > MaxKeys) {
            return word;
        }
        Word joined = run;
        FoldSpaces(joined);
        std::copy_n(word.Keys.begin(), word.Count, joined.Keys.begin() + joined.Count);
        joined.Count += word.Count;
        joined.Spaces = word.Spaces;
        joined.Judged = word.Judged;
        return joined;
    }

    /// Edited after its Space left it alone: that Space's pin goes, so the judge reads the word anew.
    inline void Reopen(Word& word) {
        if (word.Judged == Judgement::Kept || word.Judged == Judgement::Undecided) {
            word.Layout = nullptr;
        }
    }

    inline void EraseKey(Word& word, Erased& erased) {
        if (word.Judged != Judgement::None) {
            erased = {word, false};
        }
        Reopen(word);
        word.Judged = Judgement::None;
        if (!--word.Count) {
            word.Clear();
            erased.Whole = erased.Text.Count != 0;
        }
    }

    /// A key after spaces starts a new word; an undecided one joins the run unless its spaces saturated (more sit on screen).
    inline bool Rollover(Word& word, Word& run) {
        if (!word.Spaces && word.Count != MaxKeys) {
            return false;
        }
        const bool joins = word.Judged == Judgement::Undecided && word.Spaces && word.Spaces < MaxSpaces;
        run = joins ? Join(run, word) : Word{};
        word.Clear();
        return true;
    }

    inline void Append(Word& word, Erased& erased, const Convert::Key key) {
        const bool retyping =
            erased.Whole && word.Count < erased.Text.Count && erased.Text.Keys[word.Count].Vk == key.Vk;
        if (!retyping) {
            erased.Whole = false;
            erased.Text.Count = 0;
        }
        if (!word.EarlyAt || word.Judged != Judgement::Fixed) {
            Reopen(word);
            word.Judged = Judgement::None;
        }
        word.Keys[word.Count++] = key;
    }
}
