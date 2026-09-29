#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Convert {

    struct RuleHit {
        /// Kind and matched text, "B ofc"; empty when nothing switches.
        std::wstring Pattern;
        bool Anywhere = false;

        explicit operator bool() const { return !Pattern.empty(); }
    };

    /// Punto-style rules `[_FLAGS ]pattern` (flags in keyboard.md), kept as sorted 64-bit hashes, never as text.
    class Rules {
    public:
        bool Load(const std::filesystem::path& file);
        size_t Count() const { return _keys.size(); }
        /// `open`: the word goes on, so whole-word and end-anchored patterns wait for its end.
        RuleHit Find(std::wstring_view word, bool cyrillic, bool open = false) const;

    private:
        void _add(std::wstring_view line);
        bool _has(uint8_t kind, std::wstring_view text) const;
        bool _match(uint8_t kind, std::wstring_view word, RuleHit& hit, bool open) const;

        std::vector<uint64_t> _keys;
        size_t _longest = 0;
    };

}
