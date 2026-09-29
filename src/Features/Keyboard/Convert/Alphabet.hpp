#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Convert {

    class Alphabet {
    public:
        Alphabet() = default;
        explicit Alphabet(std::wstring symbols) : _symbols(std::move(symbols)) {}

        size_t Size() const { return _symbols.size(); }
        bool Empty() const { return _symbols.empty(); }
        const std::wstring& Symbols() const { return _symbols; }

        /// Padded with word edges; `open` leaves the end off.
        std::vector<uint32_t> Encode(std::wstring_view text, bool open = false) const;
        double Coverage(std::wstring_view text) const;

    private:
        /// 0 for a symbol not in the alphabet; `c` is lowercase.
        uint32_t _index(wchar_t c) const;

        std::wstring _symbols;
    };

}
