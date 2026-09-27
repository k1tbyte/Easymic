#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Convert {

    struct RuleHit {
        /// Kind and matched text for the log, "B ofc"; empty when nothing switches.
        std::wstring Pattern;
        /// A mid-word pattern: the weakest kind.
        bool Anywhere = false;

        explicit operator bool() const { return !Pattern.empty(); }
    };

    /**
     * @brief Punto-style rules: patterns in the characters of the layout a word was typed in.
     *
     * Line syntax `[_FLAGS ]pattern`: P whole word, B word begin, none or A anywhere, E never
     * switch, C case-sensitive, D skipped. A space at a pattern's edge anchors it to the word edge.
     * Kept as sorted 64-bit hashes, never as text.
     */
    class Rules {
    public:
        /// Appends one UTF-8 file.
        bool Load(const std::filesystem::path& file);
        size_t Count() const { return _keys.size(); }
        /// The pattern that switches the word, unless an exception covers it.
        RuleHit Find(std::wstring_view word, bool cyrillic) const;

    private:
        void _add(std::wstring_view line);
        bool _has(uint8_t kind, std::wstring_view text) const;
        bool _match(uint8_t kind, std::wstring_view word, RuleHit& hit) const;

        std::vector<uint64_t> _keys;
        size_t _longest = 0;
    };

}
