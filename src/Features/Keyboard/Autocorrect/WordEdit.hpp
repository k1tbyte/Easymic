#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <windows.h>

namespace WordEdit {

    inline constexpr std::array<uint8_t, 8> ModifierVks = {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
                                                           VK_LMENU,  VK_RMENU,  VK_LWIN,     VK_RWIN};

    /// The bit of a key in the modifier mask, its index in `ModifierVks`; 0 for any other key.
    inline constexpr std::array<uint8_t, 256> BitOf = [] {
        std::array<uint8_t, 256> bits{};
        for (size_t i = 0; i < ModifierVks.size(); ++i) {
            bits[ModifierVks[i]] = static_cast<uint8_t>(1u << i);
        }
        return bits;
    }();

    inline constexpr uint8_t ShiftBits = BitOf[VK_LSHIFT] | BitOf[VK_RSHIFT];
    inline constexpr uint8_t ChordBits = static_cast<uint8_t>(~ShiftBits);
    inline constexpr uint8_t AltWinBits = BitOf[VK_LMENU] | BitOf[VK_RMENU] | BitOf[VK_LWIN] | BitOf[VK_RWIN];

    std::vector<INPUT> Replace(size_t keys, size_t spaces, std::wstring_view text, uint8_t modifiers);
}
