#include "Ngram.hpp"

#include "Alphabet.hpp"

#include <cmath>

namespace Convert {

namespace {
    constexpr double kSmoothing = 0.5;
} // anonymous namespace

double NgramScore(const std::wstring& symbols, const uint32_t* tri,
                  const uint32_t* bi, const std::wstring& text, const bool open) {
    const Alphabet alphabet(symbols);
    const uint64_t v = alphabet.Size();
    const std::vector<uint32_t> seq = alphabet.Encode(text, open);
    if (seq.size() < 3) {
        return -20.0;
    }

    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 2; i < seq.size(); ++i) {
        const uint64_t ctx = seq[i - 2] * v + seq[i - 1];
        const double num = tri[ctx * v + seq[i]] + kSmoothing;
        const double den = bi[ctx] + kSmoothing * static_cast<double>(v);
        sum += std::log(num / den);
        ++n;
    }
    return n ? sum / static_cast<double>(n) : -20.0;
}

}
