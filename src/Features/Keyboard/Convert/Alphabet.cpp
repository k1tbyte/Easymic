#include "Alphabet.hpp"

#include <windows.h>

namespace Convert {

namespace {

    wchar_t _lower(wchar_t c) {
        CharLowerBuffW(&c, 1);
        return c;
    }

    uint32_t _indexOf(const std::wstring& symbols, wchar_t c) {
        const wchar_t lc = _lower(c);
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == lc) {
                return static_cast<uint32_t>(i);
            }
        }
        return 0;
    }

} // anonymous namespace

Alphabet::Alphabet(const std::wstring& symbols) : _symbols(symbols) {}

std::vector<uint32_t> Alphabet::Encode(const std::wstring& text, const bool open) const {
    std::vector<uint32_t> out;
    out.reserve(text.size() + 3);
    out.push_back(0);
    out.push_back(0);
    for (wchar_t c : text) {
        const uint32_t idx = _indexOf(_symbols, c);
        if (idx == 0 && out.back() == 0) {
            continue;
        }
        out.push_back(idx);
    }
    if (!open && out.back() != 0) {
        out.push_back(0);
    }
    return out;
}

double Alphabet::Coverage(const std::wstring& text) const {
    size_t total = 0, known = 0;
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t') {
            continue;
        }
        ++total;
        const uint32_t idx = _indexOf(_symbols, c);
        if (idx != 0) {
            ++known;
        }
    }
    return total ? static_cast<double>(known) / static_cast<double>(total) : 0.0;
}

}
