#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

/// Left and right modifiers as the bits of a mask's low byte - the bits KeyNames names.
inline constexpr std::array<uint8_t, 256> ModifierBits = [] {
    std::array<uint8_t, 256> bits{};
    bits[VK_LCONTROL] = 0x04;
    bits[VK_LSHIFT] = 0x08;
    bits[VK_LMENU] = 0x10;
    bits[VK_RCONTROL] = 0x20;
    bits[VK_RSHIFT] = 0x40;
    bits[VK_RMENU] = 0x80;
    return bits;
}();

/// Whether any key byte of a mask is a mouse button.
constexpr bool HasMouseButton(uint64_t mask) {
    for (mask >>= 8; mask; mask >>= 8) {
        switch (static_cast<uint8_t>(mask)) {
            case VK_LBUTTON: case VK_RBUTTON: case VK_MBUTTON: case VK_XBUTTON1: case VK_XBUTTON2:
                return true;
            default:
                break;
        }
    }
    return false;
}

/// The combination a stream of downs and ups spells: modifier bits in byte 0, up to seven keys
/// above it, the newest in byte 1.
struct KeyChord {
    uint64_t Mask = 0;

    void Press(const uint8_t vk) {
        if (const uint8_t bit = ModifierBits[vk]) {
            Mask |= bit;
            return;
        }
        Mask = ((Mask & ~uint64_t{0xFF}) << 8) | (uint64_t{vk} << 8) | (Mask & 0xFF);
    }

    void Release(const uint8_t vk) {
        if (const uint8_t bit = ModifierBits[vk]) {
            Mask &= ~uint64_t{bit};
            return;
        }
        // Releasing the newest key drops its byte: shift bytes 2-7 down one, keep the modifier
        // byte. Releasing anything else leaves modifiers only.
        const uint64_t modifiers = Mask & 0xFF;
        Mask = ((Mask >> 8) & 0xFF) == vk ? (((Mask >> 16) << 8) | modifiers) : modifiers;
    }
};
