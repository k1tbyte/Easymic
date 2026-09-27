#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Convert {

    class Alphabet {
    public:
        Alphabet() = default;
        explicit Alphabet(const std::wstring& symbols);

        size_t Size() const { return _symbols.size(); }
        bool Empty() const { return _symbols.empty(); }
        const std::wstring& Symbols() const { return _symbols; }

        /// Padded with word edges; `open` leaves the end off.
        std::vector<uint32_t> Encode(const std::wstring& text, bool open = false) const;
        double Coverage(const std::wstring& text) const;

    private:
        std::wstring _symbols;
    };

}
