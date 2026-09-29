#pragma once

#include <glaze/glaze.hpp>

#include "AppConfig.hpp"

/// Enums go to disk as names: "2" says nothing to a person editing the file.
template <>
struct glz::meta<MicPillMode> {
    using enum MicPillMode;
    static constexpr auto value = enumerate(Hidden, Muted, MutedOrTalk);
};

template <>
struct glz::meta<AutoCorrectMode> {
    using enum AutoCorrectMode;
    static constexpr auto value = enumerate(Off, Space, MidWord);
};
