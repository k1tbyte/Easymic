#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "Alphabet.hpp"

namespace Convert {

    constexpr uint32_t PackVersion = 2;
    inline constexpr char PackMagic[4] = {'P', 'S', 'P', '1'};
    constexpr size_t MaxAlphabet = 64;
    /// Word starts of 2 up to this many letters are in the bloom too: a word leaves its language within them.
    constexpr size_t StartLetters = 4;
    constexpr double DefaultThreshold = 0.35;
    constexpr double NoLetterScore = -20.0;

    struct PackHeader {
        char Magic[4];
        uint32_t Version;
        uint32_t AlphabetLen;
        uint32_t BloomHashes;
        uint64_t BloomBits;
        uint64_t WordCount;
        float Threshold;
        uint32_t Reserved;
        char Locale[16];
    };
    static_assert(sizeof(PackHeader) == 56);

    struct PackSections {
        uint64_t Alphabet, Bloom, Tri, Bi, Total;
    };

    inline uint64_t Align8(uint64_t x) { return (x + 7) / 8 * 8; }

    inline uint64_t BloomByteSize(uint64_t bits) { return bits / 8; }

    inline PackSections ComputeSections(uint64_t v, uint64_t bloomBits) {
        PackSections s{};
        s.Alphabet = sizeof(PackHeader);
        s.Bloom = Align8(s.Alphabet + 4 * v);
        s.Tri = Align8(s.Bloom + BloomByteSize(bloomBits));
        s.Bi = Align8(s.Tri + 4 * v * v * v);
        s.Total = s.Bi + 4 * v * v;
        return s;
    }

    std::wstring_view TrimWord(std::wstring_view s);
    std::wstring WordKey(std::wstring_view word);
    std::wstring StartKey(std::wstring_view text);

    class Pack {
    public:
        Pack() = default;
        ~Pack();
        Pack(const Pack&) = delete;
        Pack& operator=(const Pack&) = delete;

        bool Load(const std::wstring& path, std::wstring* error = nullptr);

        const char* Locale() const { return _locale; }
        double Threshold() const { return _threshold; }
        uint64_t WordCount() const { return _wordCount; }
        uint64_t MappedBytes() const { return _size; }
        const std::wstring& Symbols() const { return _alphabet.Symbols(); }

        bool Contains(std::wstring_view word) const;
        bool Begins(std::wstring_view text) const;
        /// `open`: the text goes on, so no word end is scored.
        double Score(std::wstring_view text, bool open = false) const;
        size_t Unseen(std::wstring_view text, bool open = false) const;

    private:
        void _close();

        HANDLE _mapping = nullptr;
        const uint8_t* _data = nullptr;
        uint64_t _size = 0;

        Alphabet _alphabet;
        uint64_t _bloomBits = 0;
        uint32_t _bloomHashes = 0;
        const uint8_t* _bloomData = nullptr;
        const uint32_t* _tri = nullptr;
        const uint32_t* _bi = nullptr;
        char _locale[16]{};
        double _threshold = DefaultThreshold;
        uint64_t _wordCount = 0;
    };

}
