#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "../Check.hpp"
#include "Features/Keyboard/Convert/Bloom.hpp"
#include "Features/Keyboard/Convert/Pack.hpp"
#include "Features/Keyboard/KeyboardPacks.hpp"
#include "Platform/File.hpp"

namespace {

    using namespace Convert;
    using Test::Check;

    constexpr uint32_t SymbolCount = 3;
    constexpr uint64_t BloomBitCount = 64;
    constexpr uint32_t HashCount = 2;
    constexpr uint32_t AlphabetSymbols[SymbolCount] = {L' ', L'a', L'b'};

    struct TempFile {
        std::filesystem::path Path = std::filesystem::temp_directory_path() / L"easylauncher-pack-test.pack";

        explicit TempFile(const std::string& bytes) { File::Write(Path.wstring().c_str(), bytes); }
        ~TempFile() {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

    PackHeader _header() {
        PackHeader header{};
        std::memcpy(header.Magic, PackMagic, sizeof(PackMagic));
        header.Version = PackVersion;
        header.AlphabetLen = SymbolCount;
        header.BloomHashes = HashCount;
        header.BloomBits = BloomBitCount;
        header.Threshold = 0.5f;
        std::memcpy(header.Locale, "xx", 3);
        return header;
    }

    /// A pack of zeroed tables, as long as the well-formed header describes.
    std::string _image(const PackHeader& header, const std::initializer_list<std::wstring> bloomKeys = {}) {
        const PackSections at = ComputeSections(SymbolCount, BloomBitCount);
        std::string bytes(at.Total, '\0');
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + at.Alphabet, AlphabetSymbols, sizeof(AlphabetSymbols));
        auto* bloom = reinterpret_cast<uint8_t*>(bytes.data() + at.Bloom);
        for (const std::wstring& key : bloomKeys) {
            BloomBits(key, BloomBitCount, HashCount, [bloom](const uint64_t bit) {
                bloom[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
                return true;
            });
        }
        return bytes;
    }

    void _rejects(const std::string& bytes, const std::wstring& reason, const char* what) {
        const TempFile file(bytes);
        Pack pack;
        std::wstring error;
        Check(!pack.Load(file.Path.wstring(), &error) && error == reason, what);
    }

    void _rejections() {
        Pack missing;
        std::wstring error;
        Check(!missing.Load(L"/no/such/dir/en.pack", &error) && error == L"cannot open pack", "a missing file");
        Check(!missing.Load(L"/no/such/dir/en.pack"), "no error text asked for");

        _rejects(std::string(sizeof(PackHeader) - 1, '\0'), L"pack too small", "a file shorter than the header");

        PackHeader header = _header();
        header.Magic[0] = 'X';
        _rejects(_image(header), L"not a valid pack", "a foreign magic");
        header = _header();
        header.Version = PackVersion + 1;
        _rejects(_image(header), L"not a valid pack", "another version");

        for (const uint32_t length : {0u, static_cast<uint32_t>(MaxAlphabet + 1)}) {
            header = _header();
            header.AlphabetLen = length;
            _rejects(_image(header), L"bad alphabet in pack", "an alphabet of no or too many symbols");
        }

        header = _header();
        header.BloomBits = 0;
        _rejects(_image(header), L"bad bloom in pack", "a zero bloom");
        header = _header();
        header.BloomBits = (_image(header).size() + 1) * 8;
        _rejects(_image(header), L"bad bloom in pack", "a bloom larger than the file");
        header = _header();
        header.BloomHashes = 0;
        _rejects(_image(header), L"bad bloom in pack", "a bloom without hashes");
        header.BloomHashes = MaxBloomHashes + 1;
        _rejects(_image(header), L"bad bloom in pack", "a bloom with too many hashes");

        const std::string whole = _image(_header());
        _rejects(whole.substr(0, whole.size() - 1), L"truncated pack", "a file cut short of its tables");
    }

    void _accepts() {
        const TempFile file(_image(_header(), {WordKey(L"ab"), StartKey(L"ab")}));
        Pack pack;
        Check(pack.Load(file.Path.wstring()), "a well-formed pack loads");
        Check(std::string(pack.Locale()) == "xx" && pack.Threshold() == 0.5 && pack.Symbols() == L" ab",
              "the header fields are read");
        Check(pack.Contains(L"'AB!") && pack.Begins(L"ab") && !pack.Contains(L"ba") && !pack.Begins(L"ba"),
              "the bloom finds the word and its start through trimming and case");
        Check(pack.Unseen(L"ab") == 3 && std::abs(pack.Score(L"ab") - std::log(1.0 / 3)) < 1e-9,
              "empty tables see no trigram and smooth to one third");
        Check(pack.Score(L"zz") == NoLetterScore, "text without a symbol of the alphabet scores the floor");

        Check(TrimWord(L"'Hello!") == L"Hello" && WordKey(L"-ABC,") == L"abc" && TrimWord(L"...").empty(),
              "word keys drop edge signs and case");
    }

    void _packFilenames() {
        using KeyboardPacks::ValidPackFilename;
        Check(ValidPackFilename(L"en.pack") && ValidPackFilename(L"Ru-1_x.pack"), "plain names are valid");
        Check(!ValidPackFilename(L"") && !ValidPackFilename(L".pack") && !ValidPackFilename(L"en.PACK")
                  && !ValidPackFilename(L"en.pack.exe") && !ValidPackFilename(L"en"),
              "a stem and the exact extension are needed");
        Check(!ValidPackFilename(L"../en.pack") && !ValidPackFilename(L"..\\en.pack") && !ValidPackFilename(L"a/b.pack")
                  && !ValidPackFilename(L"C:en.pack") && !ValidPackFilename(L"a.b.pack") && !ValidPackFilename(L"a b.pack")
                  && !ValidPackFilename(L"\x00e9.pack"),
              "path parts and odd characters are refused");
        const size_t stemLimit = KeyboardPacks::MaxFilename - KeyboardPacks::PackExtension.size();
        Check(ValidPackFilename(std::wstring(stemLimit, L'a') + L".pack")
                  && !ValidPackFilename(std::wstring(stemLimit + 1, L'a') + L".pack"),
              "the name length is limited");
    }

}

int main() {
    _packFilenames();
    _accepts();
    _rejections();
    if (Test::Failures) {
        return 1;
    }
    std::puts("pack checks passed");
}
