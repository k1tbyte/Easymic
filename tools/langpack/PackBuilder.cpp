#include "PackBuilder.hpp"

#include "Console.hpp"

#include "Features/Keyboard/Convert/Alphabet.hpp"
#include "Features/Keyboard/Convert/Bloom.hpp"
#include "Features/Keyboard/Convert/Pack.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <vector>

namespace {

    struct BloomParams {
        uint64_t Bits = 0;
        uint32_t Hashes = 0;
    };

    BloomParams _paramsFor(uint64_t items, double falsePositiveRate) {
        if (items == 0) {
            items = 1;
        }
        const double ln2 = std::log(2.0);
        const double bits = -static_cast<double>(items) * std::log(falsePositiveRate) / (ln2 * ln2);
        BloomParams p;
        p.Bits = (static_cast<uint64_t>(bits) + 63) / 64 * 64;
        p.Hashes = std::clamp(static_cast<uint32_t>(std::lround(bits / items * ln2)), 1u, Convert::MaxBloomHashes);
        return p;
    }

    void _bloomAdd(uint8_t* data, const BloomParams& p, const std::wstring& word) {
        Convert::BloomBits(word, p.Bits, p.Hashes, [data](const uint64_t bit) {
            data[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
            return true;
        });
    }

    void _forEachWord(const std::wstring& text, const std::function<void(const std::wstring&)>& fn) {
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find_first_of(L"\r\n", start);
            if (end == std::wstring::npos) {
                end = text.size();
            }
            std::wstring raw = text.substr(start, end - start);
            raw = raw.substr(0, raw.find(L'/'));
            const std::wstring word = Convert::WordKey(raw);
            if (!word.empty()) {
                fn(word);
            }
            if (end == text.size()) {
                break;
            }
            start = end + 1;
        }
    }

    void _writePadding(std::ofstream& out, uint64_t upTo) {
        static const char zeros[8] = {};
        const uint64_t pos = static_cast<uint64_t>(out.tellp());
        if (upTo > pos) {
            out.write(zeros, static_cast<std::streamsize>(upTo - pos));
        }
    }

    class NgramBuilder {
    public:
        explicit NgramBuilder(const Convert::Alphabet& alphabet) : _alphabet(alphabet) {
            const size_t v = _alphabet.Size();
            _tri.assign(v * v * v, 0);
            _bi.assign(v * v, 0);
        }

        void Train(const std::wstring& text) {
            const uint64_t v = _alphabet.Size();
            const std::vector<uint32_t> seq = _alphabet.Encode(text);
            for (size_t i = 2; i < seq.size(); ++i) {
                const uint64_t ctx = seq[i - 2] * v + seq[i - 1];
                ++_bi[static_cast<size_t>(ctx)];
                ++_tri[static_cast<size_t>(ctx * v + seq[i])];
            }
        }

        const std::vector<uint32_t>& Trigrams() const { return _tri; }
        const std::vector<uint32_t>& Bigrams() const { return _bi; }

    private:
        const Convert::Alphabet& _alphabet;
        std::vector<uint32_t> _tri;
        std::vector<uint32_t> _bi;
    };

} // anonymous namespace

bool BuildPack(const std::string& wordListPath, const PackOptions& options,
               const std::string& outPath, std::string* error) {
    const auto fail = [error](const std::string& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    const std::wstring text = Console::ReadFileUtf8(wordListPath);
    if (text.empty()) {
        return fail("cannot read word list: " + wordListPath);
    }

    std::map<wchar_t, uint64_t> freq;
    uint64_t words = 0;
    _forEachWord(text, [&](const std::wstring& w) {
        ++words;
        for (wchar_t c : w) {
            ++freq[c];
        }
    });
    if (words == 0) {
        return fail("word list has no usable entries");
    }

    uint64_t chars = 0;
    for (const auto& [c, n] : freq) {
        chars += n;
    }
    const uint64_t cutoff = std::max<uint64_t>(10, chars / 100000);

    std::wstring symbols = L" ";
    for (const auto& [c, n] : freq) {
        if (n >= cutoff) {
            symbols.push_back(c);
        }
    }
    if (symbols.size() > Convert::MaxAlphabet) {
        return fail("alphabet too large: " + std::to_string(symbols.size()) + " symbols");
    }

    const Convert::Alphabet alphabet(symbols);
    NgramBuilder ngram(alphabet);
    const BloomParams params = _paramsFor(words, options.FalsePositiveRate);
    std::vector<uint8_t> bits(static_cast<size_t>(Convert::BloomByteSize(params.Bits)), 0);

    _forEachWord(text, [&](const std::wstring& w) {
        ngram.Train(w);
        _bloomAdd(bits.data(), params, w);
    });

    Convert::PackHeader header{};
    std::copy_n(Convert::PackMagic, 4, header.Magic);
    header.Version = Convert::PackVersion;
    header.AlphabetLen = static_cast<uint32_t>(symbols.size());
    header.BloomHashes = params.Hashes;
    header.BloomBits = params.Bits;
    header.WordCount = words;
    header.Threshold = static_cast<float>(options.Threshold);
    std::copy_n(options.Locale.c_str(),
                std::min<size_t>(options.Locale.size(), 15), header.Locale);

    const Convert::PackSections at = Convert::ComputeSections(symbols.size(), params.Bits);
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        return fail("cannot write pack: " + outPath);
    }

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (wchar_t c : symbols) {
        const uint32_t cp = static_cast<uint32_t>(c);
        out.write(reinterpret_cast<const char*>(&cp), sizeof(cp));
    }
    _writePadding(out, at.Bloom);
    out.write(reinterpret_cast<const char*>(bits.data()),
              static_cast<std::streamsize>(bits.size()));
    _writePadding(out, at.Tri);
    out.write(reinterpret_cast<const char*>(ngram.Trigrams().data()),
              static_cast<std::streamsize>(ngram.Trigrams().size() * 4));
    _writePadding(out, at.Bi);
    out.write(reinterpret_cast<const char*>(ngram.Bigrams().data()),
              static_cast<std::streamsize>(ngram.Bigrams().size() * 4));
    return out.good() ? true : fail("write failed: " + outPath);
}
