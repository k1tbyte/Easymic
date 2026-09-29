#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>

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

/// A mask keeps modifier bits in byte 0 and up to seven keys above it, the newest in byte 1.
constexpr uint64_t SingleKey(const uint8_t vk) {
    return uint64_t{vk} << 8;
}

struct KeyList {
    std::array<uint8_t, 7> Vk{};
    size_t Count = 0;

    const uint8_t* begin() const { return Vk.data(); }
    const uint8_t* end() const { return Vk.data() + Count; }
};

/// The keys of a mask in the order they were pressed.
constexpr KeyList KeysOf(uint64_t mask) {
    KeyList keys;
    for (mask >>= 8; mask & 0xFF; mask >>= 8) {
        keys.Vk[keys.Count++] = static_cast<uint8_t>(mask);
    }
    std::reverse(keys.Vk.begin(), keys.Vk.begin() + keys.Count);
    return keys;
}

/// A modifier VK is held when its bit is set; any other VK when it is among the keys.
constexpr bool Contains(const uint64_t mask, const uint8_t vk) {
    if (const uint8_t bit = ModifierBits[vk]) {
        return (mask & bit) != 0;
    }
    for (const uint8_t key : KeysOf(mask)) {
        if (key == vk) {
            return true;
        }
    }
    return false;
}

constexpr bool HasMouseButton(const uint64_t mask) {
    for (const uint8_t vk : KeysOf(mask)) {
        switch (vk) {
            case VK_LBUTTON: case VK_RBUTTON: case VK_MBUTTON: case VK_XBUTTON1: case VK_XBUTTON2:
                return true;
            default:
                break;
        }
    }
    return false;
}

struct KeyChord {
    uint64_t Mask = 0;

    void Press(const uint8_t vk) {
        if (const uint8_t bit = ModifierBits[vk]) {
            Mask |= bit;
            return;
        }
        Push(vk);
    }

    void Release(const uint8_t vk) {
        if (const uint8_t bit = ModifierBits[vk]) {
            Mask &= ~uint64_t{bit};
            return;
        }
        // Only the newest key's release keeps the keys below it; any other ends the combination
        const uint64_t modifiers = Mask & 0xFF;
        Mask = ((Mask >> 8) & 0xFF) == vk ? (((Mask >> 16) << 8) | modifiers) : modifiers;
    }

    void Push(const uint8_t vk) {
        Mask = ((Mask & ~uint64_t{0xFF}) << 8) | SingleKey(vk) | (Mask & 0xFF);
    }
};
