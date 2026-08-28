#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/**
 * @brief The two directions between a key mask and its printable name.
 *
 * A mask packs the modifier bits into byte 0 and the keys into bytes 1-7, the most recently
 * pressed one first. Format spells that out in press order, Parse reads it back.
 *
 * Parse exists because the config stores the name rather than the number: a packed uint64 is the
 * one field in a binding nobody can read or edit by hand, and it is the interesting one.
 */
namespace KeyNames {

    /// "CTRL + SHIFT + M". Empty for an empty mask. A key with no name in the table is printed
    /// as its hex code, which Parse reads back.
    std::string Format(uint64_t keysMask);

    /**
     * @brief The mask a name describes, or 0 when it does not describe one.
     *
     * Tokens are separated by '+' and matched without regard to case or surrounding spaces.
     *
     * A modifier name wins over a key of the same spelling, which is what makes the round trip
     * total: the hook folds all six side-specific modifier keys into the modifier byte and never
     * reports the three generic ones at all, so none of them can reach a key byte and their names
     * are free to mean the modifier. That is also why VK_OEM_PLUS is called "Plus" rather than
     * "+" - it is the one key whose name would otherwise be the separator itself.
     *
     * An unknown token rejects the whole name rather than silently dropping one key: half a
     * combination is a hotkey the user never asked for.
     */
    uint64_t Parse(std::string_view name);
}
