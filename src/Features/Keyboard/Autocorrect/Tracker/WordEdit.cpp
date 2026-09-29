#include "WordEdit.hpp"

namespace WordEdit {
namespace {
    /// Tapped between the press and release of Alt or Win: the release alone would open the menu or Start.
    constexpr WORD UnassignedVk = 0xE8;
    constexpr uint8_t RestoreBits = static_cast<uint8_t>(~AltWinBits);

    INPUT _key(const WORD vk, const bool up) {
        const bool extended = vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
        INPUT input{.type = INPUT_KEYBOARD};
        input.ki = {.wVk = vk,
                    .wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)),
                    .dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | (extended ? KEYEVENTF_EXTENDEDKEY : 0u)};
        return input;
    }

    void _tap(std::vector<INPUT>& edit, const WORD vk, const size_t times = 1) {
        const INPUT pair[] = {_key(vk, false), _key(vk, true)};
        for (size_t i = 0; i < times; ++i) {
            edit.insert(edit.end(), std::begin(pair), std::end(pair));
        }
    }

    void _type(std::vector<INPUT>& edit, const wchar_t c) {
        for (const DWORD up : {0ul, static_cast<DWORD>(KEYEVENTF_KEYUP)}) {
            INPUT input{.type = INPUT_KEYBOARD};
            input.ki = {.wScan = c, .dwFlags = KEYEVENTF_UNICODE | up};
            edit.push_back(input);
        }
    }

    void _press(std::vector<INPUT>& edit, const uint8_t modifiers, const bool up) {
        for (size_t i = 0; i < ModifierVks.size(); ++i) {
            if (modifiers & (1u << i)) {
                edit.push_back(_key(ModifierVks[i], up));
            }
        }
    }
}

std::vector<INPUT> Replace(const size_t keys, const size_t spaces,
                           const std::wstring_view text, const uint8_t modifiers) {
    std::vector<INPUT> edit;
    edit.reserve(2 * (keys + 2 * spaces + text.size()) + 2 * ModifierVks.size() + 2);
    if (modifiers & AltWinBits) {
        _tap(edit, UnassignedVk);
    }
    _press(edit, modifiers, true);
    _tap(edit, VK_BACK, keys + spaces);
    for (const wchar_t c : text) {
        _type(edit, c);
    }
    for (size_t i = 0; i < spaces; ++i) {
        _type(edit, L' ');
    }
    _press(edit, modifiers & RestoreBits, false);
    return edit;
}
}
