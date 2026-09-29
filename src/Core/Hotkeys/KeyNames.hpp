#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace KeyNames {

    /// "CTRL + SHIFT + M" in press order. A key with no name prints as hex, which Parse reads back.
    std::string Format(uint64_t keysMask);

    /// 0 unless every '+'-separated token names a key or modifier: half a combination is a hotkey
    /// nobody asked for. VK_OEM_PLUS is "Plus".
    uint64_t Parse(std::string_view name);
}
