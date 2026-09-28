#pragma once

#include <cstdint>
#include <string>

namespace Convert {

    double NgramScore(const std::wstring& symbols, const uint32_t* tri,
                      const uint32_t* bi, const std::wstring& text, bool open = false);

    /// Trigrams of `text` that no word of the pack has: a word of its language has none.
    size_t NgramUnseen(const std::wstring& symbols, const uint32_t* tri, const std::wstring& text, bool open = false);

}
