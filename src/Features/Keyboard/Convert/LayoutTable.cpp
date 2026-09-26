#include "LayoutTable.hpp"

namespace Convert {

LayoutTable::LayoutTable(const HKL layout) : _layout(layout) {
    for (UINT vk = 0x20; vk <= 0xFF; ++vk) {
        // The numpad repeats the main block and follows the locale number format: its period
        // turns into a comma
        if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) {
            continue;
        }
        const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout);
        if (!scan) {
            continue;
        }
        for (const bool shift : {false, true}) {
            BYTE state[256]{};
            state[VK_SHIFT] = shift ? 0x80 : 0;
            wchar_t out[8]{};
            // Flag 4 leaves this thread's dead-key state alone (Windows 10 1607+)
            if (ToUnicodeEx(vk, scan, state, out, 8, 0x4, layout) == 1 && out[0] >= 0x20 && out[0] != 0x7F) {
                (shift ? _shifted : _plain)[vk] = out[0];
            }
        }
        wchar_t lower = _shifted[vk];
        CharLowerBuffW(&lower, 1);
        _letter[vk] = _plain[vk] && _shifted[vk] != _plain[vk] && lower == _plain[vk];
    }
}

wchar_t LayoutTable::Char(const Key key) const {
    const bool shift = _letter[key.Vk] ? key.Shift != key.Caps : key.Shift;
    return (shift ? _shifted : _plain)[key.Vk];
}

std::wstring LayoutTable::Render(const std::span<const Key> keys) const {
    std::wstring text;
    text.reserve(keys.size());
    for (const Key key : keys) {
        const wchar_t c = Char(key);
        if (!c) {
            return {};
        }
        text.push_back(c);
    }
    return text;
}

}
