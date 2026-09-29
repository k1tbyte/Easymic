#include "PackBuilder.hpp"

#include "Features/Keyboard/Convert/Alphabet.hpp"
#include "Features/Keyboard/Convert/Bloom.hpp"
#include "Platform/File.hpp"
#include "Platform/Str.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace {

    constexpr uint64_t MinSymbolCount = 10;
    constexpr uint64_t SymbolRarity = 100000;

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

    void _bloomAdd(uint8_t* data, const BloomParams& p, const std::wstring_view word) {
        Convert::BloomBits(word, p.Bits, p.Hashes, [data](const uint64_t bit) {
            data[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
            return true;
        });
    }

    std::wstring _readText(const std::filesystem::path& path) {
        auto bytes = File::Read(path.wstring().c_str());
        if (!bytes) {
            return {};
        }
        if (bytes->starts_with("\xEF\xBB\xBF")) {
            bytes->erase(0, 3);
        }
        return Str::Utf8ToWide(*bytes);
    }

    /// One word per line, the part after a `/` (a hunspell flag list) dropped.
    std::vector<std::wstring> _readWords(const std::wstring_view text) {
        std::vector<std::wstring> words;
        for (size_t start = 0; start < text.size();) {
            const size_t end = std::min(text.find_first_of(L"\r\n", start), text.size());
            const std::wstring_view line = text.substr(start, end - start);
            if (std::wstring word = Convert::WordKey(line.substr(0, line.find(L'/'))); !word.empty()) {
                words.push_back(std::move(word));
            }
            start = end + 1;
        }
        return words;
    }

    std::wstring _alphabetOf(const std::map<wchar_t, uint64_t>& frequency) {
        uint64_t chars = 0;
        for (const auto& [symbol, count] : frequency) {
            chars += count;
        }
        const uint64_t cutoff = std::max(MinSymbolCount, chars / SymbolRarity);
        std::wstring symbols = L" ";
        for (const auto& [symbol, count] : frequency) {
            if (count >= cutoff) {
                symbols.push_back(symbol);
            }
        }
        return symbols;
    }

    class NgramBuilder {
    public:
        explicit NgramBuilder(const Convert::Alphabet& alphabet) : _alphabet(alphabet) {
            const size_t v = _alphabet.Size();
            _tri.assign(v * v * v, 0);
            _bi.assign(v * v, 0);
        }

        void Train(const std::wstring_view text) {
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

    void _appendAt(std::string& image, const uint64_t offset, const void* data, const size_t size) {
        image.resize(std::max<size_t>(image.size(), offset));
        image.append(static_cast<const char*>(data), size);
    }

    std::string _image(const Convert::PackHeader& header, const std::wstring& symbols,
                       const std::vector<uint8_t>& bloom, const NgramBuilder& ngram) {
        const Convert::PackSections at = Convert::ComputeSections(symbols.size(), header.BloomBits);
        std::string image;
        _appendAt(image, 0, &header, sizeof(header));
        for (const wchar_t c : symbols) {
            const uint32_t codepoint = static_cast<uint32_t>(c);
            _appendAt(image, at.Alphabet, &codepoint, sizeof(codepoint));
        }
        _appendAt(image, at.Bloom, bloom.data(), bloom.size());
        _appendAt(image, at.Tri, ngram.Trigrams().data(), ngram.Trigrams().size() * sizeof(uint32_t));
        _appendAt(image, at.Bi, ngram.Bigrams().data(), ngram.Bigrams().size() * sizeof(uint32_t));
        return image;
    }

} // anonymous namespace

bool BuildPack(const std::filesystem::path& wordList, const PackOptions& options,
               const std::filesystem::path& out, std::string* error) {
    const auto fail = [error](const std::string& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    const std::wstring text = _readText(wordList);
    if (text.empty()) {
        return fail("cannot read word list: " + wordList.string());
    }
    const std::vector<std::wstring> words = _readWords(text);
    if (words.empty()) {
        return fail("word list has no usable entries");
    }

    std::map<wchar_t, uint64_t> frequency;
    std::set<std::wstring> starts;
    for (const std::wstring& word : words) {
        for (const wchar_t c : word) {
            ++frequency[c];
        }
        for (size_t n = 2; n <= std::min(word.size(), Convert::StartLetters); ++n) {
            starts.insert(Convert::StartKey(std::wstring_view(word).substr(0, n)));
        }
    }

    const std::wstring symbols = _alphabetOf(frequency);
    if (symbols.size() > Convert::MaxAlphabet) {
        return fail("alphabet too large: " + std::to_string(symbols.size()) + " symbols");
    }

    const Convert::Alphabet alphabet(symbols);
    NgramBuilder ngram(alphabet);
    const BloomParams params = _paramsFor(words.size() + starts.size(), options.FalsePositiveRate);
    std::vector<uint8_t> bloom(static_cast<size_t>(Convert::BloomByteSize(params.Bits)), 0);
    for (const std::wstring& word : words) {
        ngram.Train(word);
        _bloomAdd(bloom.data(), params, word);
    }
    for (const std::wstring& start : starts) {
        _bloomAdd(bloom.data(), params, start);
    }

    Convert::PackHeader header{};
    std::copy_n(Convert::PackMagic, 4, header.Magic);
    header.Version = Convert::PackVersion;
    header.AlphabetLen = static_cast<uint32_t>(symbols.size());
    header.BloomHashes = params.Hashes;
    header.BloomBits = params.Bits;
    header.WordCount = words.size();
    header.Threshold = static_cast<float>(options.Threshold);
    std::copy_n(options.Locale.c_str(), std::min<size_t>(options.Locale.size(), 15), header.Locale);

    if (!File::Write(out.wstring().c_str(), _image(header, symbols, bloom, ngram))) {
        return fail("cannot write pack: " + out.string());
    }
    return true;
}
