#pragma once

#include <windows.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <string>

namespace Convert {

    /// One typed key as the hook saw it: a position, never a character.
    struct Key {
        uint8_t Vk;
        bool Shift;
        bool Caps;
    };

    /// Unicode block of a letter, Latin variants as one (0).
    constexpr uint8_t ScriptOf(const wchar_t c) { return c < 0x250 ? 0 : static_cast<uint8_t>(c >> 8); }
    inline constexpr uint8_t CyrillicScript = 4;

    /// What each key position types in one layout. No reverse map: a word is kept as positions and rendered in the other table.
    class LayoutTable {
    public:
        explicit LayoutTable(HKL layout);

        HKL Layout() const { return _layout; }
        wchar_t Char(Key key) const;
        /// Empty when any of the keys types nothing here.
        std::wstring Render(std::span<const Key> keys) const;
        uint8_t Script() const;

    private:
        HKL _layout;
        std::array<wchar_t, 256> _plain{};
        std::array<wchar_t, 256> _shifted{};
        /// Caps Lock flips Shift only where the shifted char is the capital of the plain one.
        std::bitset<256> _letter;
    };
}
