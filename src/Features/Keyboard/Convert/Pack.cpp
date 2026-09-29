#include "Pack.hpp"

#include "Bloom.hpp"

#include <cmath>
#include <cstring>

#include "Platform/Str.hpp"

namespace Convert {

std::wstring_view TrimWord(std::wstring_view s) {
    const auto isEdge = [](const wchar_t c) {
        return c < 128 && !((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'));
    };
    while (!s.empty() && isEdge(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isEdge(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::wstring WordKey(const std::wstring_view word) {
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
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return fail(L"cannot open pack");
    }

    LARGE_INTEGER fileSize{};
    const bool sized = GetFileSizeEx(file, &fileSize) && fileSize.QuadPart >= static_cast<LONGLONG>(sizeof(PackHeader));
    if (sized) {
        _size = static_cast<uint64_t>(fileSize.QuadPart);
        _mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    }
    CloseHandle(file);
    if (!sized) {
        return fail(L"pack too small");
    }
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
    if (!header.BloomBits || BloomByteSize(header.BloomBits) > _size || !header.BloomHashes
        || header.BloomHashes > MaxBloomHashes) {
        return fail(L"bad bloom in pack");
    }
    const PackSections at = ComputeSections(header.AlphabetLen, header.BloomBits);
    if (_size < at.Total) {
        return fail(L"truncated pack");
    }

    std::wstring symbols;
    const auto* codepoints = reinterpret_cast<const uint32_t*>(_data + at.Alphabet);
    for (uint32_t i = 0; i < header.AlphabetLen; ++i) {
        symbols.push_back(static_cast<wchar_t>(codepoints[i]));
    }
    _alphabet = Alphabet(std::move(symbols));
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

double Pack::Score(const std::wstring_view text, const bool open) const {
    constexpr double Smoothing = 0.5;
    const uint64_t v = _alphabet.Size();
    const std::vector<uint32_t> seq = _alphabet.Encode(text, open);
    if (seq.size() < 3) {
        return NoLetterScore;
    }
    double sum = 0.0;
    for (size_t i = 2; i < seq.size(); ++i) {
        const uint64_t context = seq[i - 2] * v + seq[i - 1];
        sum += std::log((_tri[context * v + seq[i]] + Smoothing)
                        / (_bi[context] + Smoothing * static_cast<double>(v)));
    }
    return sum / static_cast<double>(seq.size() - 2);
}

size_t Pack::Unseen(const std::wstring_view text, const bool open) const {
    const uint64_t v = _alphabet.Size();
    const std::vector<uint32_t> seq = _alphabet.Encode(text, open);
    size_t unseen = 0;
    for (size_t i = 2; i < seq.size(); ++i) {
        unseen += _tri[(seq[i - 2] * v + seq[i - 1]) * v + seq[i]] == 0;
    }
    return unseen;
}

}
