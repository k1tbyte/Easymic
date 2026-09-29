#pragma once

#include "Features/Keyboard/Autocorrect/Tracker/TypedWord.hpp"

namespace Fixture {

    inline TypedWord::Word Word(const char* keys, const uint8_t spaces = 0, const HKL layout = nullptr) {
        TypedWord::Word word{.Spaces = spaces, .Layout = layout};
        for (; *keys; ++keys) {
            word.Keys[word.Count++] = {static_cast<uint8_t>(*keys), false, false};
        }
        return word;
    }
}
