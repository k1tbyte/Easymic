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

    /**
     * @brief What each key position types in one layout, built from the system layout.
     *
     * No reverse map: a word is kept as positions, so converting it is rendering the same
     * positions in the other table.
     */
    class LayoutTable {
    public:
        explicit LayoutTable(HKL layout);

        HKL Layout() const { return _layout; }
        /// 0 when the position types nothing here.
        wchar_t Char(Key key) const;
        /// Empty when any of the keys types nothing here.
        std::wstring Render(std::span<const Key> keys) const;

    private:
        HKL _layout;
        std::array<wchar_t, 256> _plain{};
        std::array<wchar_t, 256> _shifted{};
        /// Caps Lock flips Shift only where the shifted char is the capital of the plain one.
        std::bitset<256> _letter;
    };
}
