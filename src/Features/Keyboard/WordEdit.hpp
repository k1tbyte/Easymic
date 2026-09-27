#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <windows.h>

namespace WordEdit {
    inline constexpr std::array<uint8_t, 8> ModifierVks = {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
                                                           VK_LMENU,  VK_RMENU,  VK_LWIN,     VK_RWIN};
    /// Bits of the modifier mask, in `ModifierVks` order.
    inline constexpr uint8_t ShiftBits = 0x03;
    inline constexpr uint8_t ChordBits = 0xFC;

    /// The bit of `vk` in the mask, 0 for any other key.
    inline uint8_t ModifierBit(const uint8_t vk) {
        const auto it = std::ranges::find(ModifierVks, vk);
        return it == ModifierVks.end() ? 0 : static_cast<uint8_t>(1u << (it - ModifierVks.begin()));
    }

    std::vector<INPUT> Replace(size_t keys, size_t spaces, std::wstring_view text, uint8_t modifiers);
}
