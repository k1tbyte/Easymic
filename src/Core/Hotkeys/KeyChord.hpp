#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

/// A modifier key, its bit in a mask's low byte and its names, in the order names are printed.
struct Modifier {
    uint8_t Vk;
    uint8_t Bit;
    const char* Name;
    /// Read, never printed: the unprefixed name already is the left key.
    const char* Alias;
};

inline constexpr Modifier Modifiers[] = {
    {VK_LCONTROL, 0x04, "CTRL", "LCTRL"},
    {VK_RCONTROL, 0x20, "RCTRL", nullptr},
    {VK_LSHIFT, 0x08, "SHIFT", "LSHIFT"},
    {VK_RSHIFT, 0x40, "RSHIFT", nullptr},
    {VK_LMENU, 0x10, "ALT", "LALT"},
    {VK_RMENU, 0x80, "RALT", nullptr},
};

inline constexpr std::array<uint8_t, 256> ModifierBits = [] {
    std::array<uint8_t, 256> bits{};
    for (const Modifier& modifier : Modifiers) {
        bits[modifier.Vk] = modifier.Bit;
    }
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
