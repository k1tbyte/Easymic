#pragma once

#include <cstdint>
#include <string_view>

namespace Convert {

    inline constexpr uint32_t MaxBloomHashes = 24;
    inline constexpr uint64_t FnvPrime = 1099511628211ull;

    inline uint64_t FnvStep(uint64_t h, const wchar_t c) {
        h = (h ^ static_cast<uint8_t>(c)) * FnvPrime;
        return (h ^ static_cast<uint8_t>(c >> 8)) * FnvPrime;
    }

    /// The basis is a digit short of the FNV-1a one: stored packs are hashed with it.
    inline uint64_t Fnv1a(const std::wstring_view text) {
        uint64_t h = 1469598103934665603ull;
        for (const wchar_t c : text) {
            h = FnvStep(h, c);
        }
        return h;
    }

    inline uint64_t Splitmix(uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    /// The builder sets these bits, a lookup tests them.
    inline bool BloomBits(const std::wstring_view word, const uint64_t bits, const uint32_t hashes,
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

    inline bool BloomContains(const uint8_t* data, const uint64_t bits, const uint32_t hashes,
                              const std::wstring_view word) {
        return BloomBits(word, bits, hashes, [data](const uint64_t bit) { return (data[bit >> 3] >> (bit & 7)) & 1u; });
    }

}
