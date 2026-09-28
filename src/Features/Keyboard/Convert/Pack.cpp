#include "Pack.hpp"

#include "Alphabet.hpp"
#include "Bloom.hpp"
#include "Ngram.hpp"

#include <algorithm>
#include <cstring>

#include "Platform/Str.hpp"

namespace Convert {

std::wstring TrimWord(std::wstring_view s) {
    const auto isEdge = [](const wchar_t c) {
        return c < 128 && !((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'));
    };
    size_t b = 0, e = s.size();
    while (b < e && isEdge(s[b])) {
        ++b;
    }
    while (e > b && isEdge(s[e - 1])) {
        --e;
    }
    return std::wstring(s.substr(b, e - b));
}

std::wstring WordKey(std::wstring_view word) {
    return Str::Lower(TrimWord(word));
}

std::wstring StartKey(const std::wstring_view text) {
    // The marker keeps a start apart from the word it spells
    return Str::Lower(text.substr(0, StartLetters)) + L'\x1';
}

Pack::~Pack() {
    _close();
}

void Pack::_close() {
    if (_data) {
        UnmapViewOfFile(_data);
        _data = nullptr;
    }
    if (_mapping) {
        CloseHandle(_mapping);
        _mapping = nullptr;
    }
    if (_file != INVALID_HANDLE_VALUE) {
        CloseHandle(_file);
        _file = INVALID_HANDLE_VALUE;
    }
}

bool Pack::Load(const std::wstring& path, std::wstring* error) {
    const auto fail = [&](const wchar_t* msg) {
        _close();
        if (error) {
            *error = msg;
        }
        return false;
    };

    _close();
    _file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (_file == INVALID_HANDLE_VALUE) {
        return fail(L"cannot open pack");
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(_file, &fileSize) ||
        fileSize.QuadPart < static_cast<LONGLONG>(sizeof(PackHeader))) {
        return fail(L"pack too small");
    }
    _size = static_cast<uint64_t>(fileSize.QuadPart);

    _mapping = CreateFileMappingW(_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!_mapping) {
        return fail(L"cannot map pack");
    }
    _data = static_cast<const uint8_t*>(MapViewOfFile(_mapping, FILE_MAP_READ, 0, 0, 0));
    if (!_data) {
        return fail(L"cannot map view");
    }

    PackHeader header{};
    std::memcpy(&header, _data, sizeof(header));
    if (std::memcmp(header.Magic, PackMagic, sizeof(PackMagic)) != 0 || header.Version != PackVersion) {
        return fail(L"not a valid pack");
    }
    if (header.AlphabetLen == 0 || header.AlphabetLen > MaxAlphabet) {
        return fail(L"bad alphabet in pack");
    }
    // A zero bloom divides by zero on lookup; one larger than the file wraps the offsets below
    if (!header.BloomBits || header.BloomBits / 8 > _size || !header.BloomHashes
        || header.BloomHashes > MaxBloomHashes) {
        return fail(L"bad bloom in pack");
    }
    const PackSections at = ComputeSections(header.AlphabetLen, header.BloomBits);
    if (_size < at.Total) {
        return fail(L"truncated pack");
    }

    _symbols.clear();
    const auto* codepoints = reinterpret_cast<const uint32_t*>(_data + at.Alphabet);
    for (uint32_t i = 0; i < header.AlphabetLen; ++i) {
        _symbols.push_back(static_cast<wchar_t>(codepoints[i]));
    }
    _bloomBits = header.BloomBits;
    _bloomHashes = header.BloomHashes;
    _bloomData = _data + at.Bloom;
    _tri = reinterpret_cast<const uint32_t*>(_data + at.Tri);
    _bi = reinterpret_cast<const uint32_t*>(_data + at.Bi);
    // The last byte stays 0: a foreign header may fill the field
    std::memcpy(_locale, header.Locale, sizeof(_locale) - 1);
    _threshold = header.Threshold;
    _wordCount = header.WordCount;
    return true;
}

bool Pack::Contains(std::wstring_view word) const {
    const std::wstring key = WordKey(word);
    return !key.empty() && BloomContains(_bloomData, _bloomBits, _bloomHashes, key);
}

bool Pack::Begins(const std::wstring_view text) const {
    return BloomContains(_bloomData, _bloomBits, _bloomHashes, StartKey(text));
}

double Pack::Score(const std::wstring& text, const bool open) const {
    return NgramScore(_symbols, _tri, _bi, text, open);
}

size_t Pack::Unseen(const std::wstring& text, const bool open) const {
    return NgramUnseen(_symbols, _tri, text, open);
}

}
