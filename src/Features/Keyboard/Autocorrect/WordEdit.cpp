#include "WordEdit.hpp"

namespace WordEdit {
namespace {
    constexpr WORD MaskVk = 0xE8;
    constexpr uint8_t AltWinBits = 0xF0;
    constexpr uint8_t RestoreBits = 0x0F;

    void _key(std::vector<INPUT>& edit, const WORD vk, const bool up) {
        const bool extended = vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
        INPUT input{.type = INPUT_KEYBOARD};
        input.ki = {.wVk = vk,
                    .wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)),
                    .dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | (extended ? KEYEVENTF_EXTENDEDKEY : 0u)};
        edit.push_back(input);
    }

    void _tap(std::vector<INPUT>& edit, const WORD vk) {
        _key(edit, vk, false);
        _key(edit, vk, true);
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
                _key(edit, ModifierVks[i], up);
            }
        }
    }
}

std::vector<INPUT> Replace(const size_t keys, const size_t spaces,
                           const std::wstring_view text, const uint8_t modifiers) {
    std::vector<INPUT> edit;
    edit.reserve(4 * (keys + spaces) + 2 * ModifierVks.size() + 2);
    if (modifiers & AltWinBits) {
        _tap(edit, MaskVk);
    }
    _press(edit, modifiers, true);
    for (size_t i = 0; i < keys + spaces; ++i) {
        _tap(edit, VK_BACK);
    }
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
