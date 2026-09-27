#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "Convert/LayoutTable.hpp"

namespace TypedWord {

    inline constexpr size_t MaxKeys = 64;
    inline constexpr uint8_t MaxSpaces = 8;

    /// The keys a word is made of; anything else ends the text the tracker follows.
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

    /// What autocorrect made of the word on its Space: the user's next move on it teaches.
    enum class Judgement : uint8_t { None, Kept, Undecided, Fixed };

    /// A word as the hook saw it: key positions and the spaces typed after it.
    struct Word {
        std::array<Convert::Key, MaxKeys> Keys{};
        uint8_t Count = 0;
        uint8_t Spaces = 0;
        /// The layout it is on screen in; null until a Space or a conversion pins it.
        HKL Layout = nullptr;
        HWND Window = nullptr;
        /// Undecided joins the run when the next word starts.
        Judgement Judged = Judgement::None;

        void Clear() {
            Count = 0;
            Spaces = 0;
            Layout = nullptr;
            Judged = Judgement::None;
        }
    };

    /// `run`, its spaces as keys, then `word`: one text. Just `word` when they differ in layout or overflow.
    inline Word Join(const Word& run, const Word& word) {
        if (!run.Count || run.Layout != word.Layout || run.Count + run.Spaces + word.Count > MaxKeys) {
            return word;
        }
        Word joined = run;
        std::fill_n(joined.Keys.begin() + joined.Count, run.Spaces, Convert::Key{VK_SPACE, false, false});
        joined.Count += run.Spaces;
        std::copy_n(word.Keys.begin(), word.Count, joined.Keys.begin() + joined.Count);
        joined.Count += word.Count;
        joined.Spaces = word.Spaces;
        joined.Judged = word.Judged;
        return joined;
    }
}
