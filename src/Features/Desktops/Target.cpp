#include "Target.hpp"

#include <charconv>

namespace Desktops {

    std::optional<Target> ParseTarget(const std::string& args) {
        if (args.empty()) {
            return std::nullopt;
        }
        if (args == "next") {
            return Target{.Step = 1};
        }
        if (args == "prev") {
            return Target{.Step = -1};
        }

        int number = 0;
        const char* const end = args.data() + args.size();
        if (const auto [at, error] = std::from_chars(args.data(), end, number); at == end && error == std::errc{}) {
            return number >= 1 ? std::optional(Target{.Index = number - 1}) : std::nullopt;
        }
        return Target{.Name = args};
    }
}
