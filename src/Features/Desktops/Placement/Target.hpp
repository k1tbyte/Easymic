#pragma once

#include <optional>
#include <string>

namespace Desktops {

    struct Target {
        int Index = -1;
        int Step = 0;
        std::string Name;
    };

    /// Nothing for an empty argument or a number below 1.
    std::optional<Target> ParseTarget(const std::string& args);
}
