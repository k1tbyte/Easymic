#pragma once

#include <string>
#include <string_view>

namespace CommandLine {

    inline constexpr char DirToken[] = "{dir}";

    struct Parts {
        std::wstring File;
        std::wstring Params;
    };

    using PathExists = bool (*)(const std::wstring& path);

    /// Only a quoted path may hold spaces: probing every prefix costs a filesystem call each, and a network path can block.
    Parts Split(const std::wstring& command, PathExists exists);

    /// A value the template already wrapped in quotes goes in bare.
    std::string Quote(std::string value, bool alreadyQuoted);

    std::string ExpandDir(std::string command, std::string_view folder);

    std::string Flatten(std::string text, size_t limit);

    std::string_view WholeCodePoints(std::string_view text);
}
