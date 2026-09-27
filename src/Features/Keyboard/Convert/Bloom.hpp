#pragma once

#include <cstdint>
#include <string>

namespace Convert {

    inline constexpr uint32_t MaxBloomHashes = 24;

    inline uint64_t Fnv1a(const std::wstring& s) {
        uint64_t h = 1469598103934665603ull;
        for (wchar_t c : s) {
            const uint16_t u = static_cast<uint16_t>(c);
            h = (h ^ static_cast<uint8_t>(u & 0xFF)) * 1099511628211ull;
            h = (h ^ static_cast<uint8_t>(u >> 8)) * 1099511628211ull;
        }
        return h;
    }

    inline uint64_t Splitmix(uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    /// The `bits` indices of `word`, one per hash: the builder sets them, a lookup tests them.
    inline bool BloomBits(const std::wstring& word, uint64_t bits, uint32_t hashes,
                          const auto& visit) {
        const uint64_t h1 = Fnv1a(word);
        const uint64_t h2 = Splitmix(h1) | 1ull;
        for (uint32_t i = 0; i < hashes; ++i) {
            if (!visit((h1 + static_cast<uint64_t>(i) * h2) % bits)) {
                return false;
            }
        }
        return true;
    }

    inline bool BloomContains(const uint8_t* data, uint64_t bits, uint32_t hashes,
                              const std::wstring& word) {
        return BloomBits(word, bits, hashes, [data](const uint64_t bit) { return (data[bit >> 3] >> (bit & 7)) & 1u; });
    }

}
