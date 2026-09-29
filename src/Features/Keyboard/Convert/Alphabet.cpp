#include "Alphabet.hpp"

#include "Platform/Str.hpp"

namespace Convert {

uint32_t Alphabet::_index(const wchar_t c) const {
    const size_t at = _symbols.find(c);
    return at == std::wstring::npos ? 0 : static_cast<uint32_t>(at);
}

std::vector<uint32_t> Alphabet::Encode(const std::wstring_view text, const bool open) const {
    std::vector<uint32_t> out;
    out.reserve(text.size() + 3);
    out.push_back(0);
    out.push_back(0);
    for (const wchar_t c : Str::Lower(text)) {
        const uint32_t idx = _index(c);
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

double Alphabet::Coverage(const std::wstring_view text) const {
    size_t total = 0, known = 0;
    for (const wchar_t c : Str::Lower(text)) {
        if (c == L' ' || c == L'\t') {
            continue;
        }
        ++total;
        known += _index(c) != 0;
    }
    return total ? static_cast<double>(known) / static_cast<double>(total) : 0.0;
}

}
