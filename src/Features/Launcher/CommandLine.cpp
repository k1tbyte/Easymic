#include "CommandLine.hpp"

#include <algorithm>

namespace CommandLine {

    Parts Split(const std::wstring& command, const PathExists exists) {
        Parts parts;
        size_t end;

        if (command.starts_with(L'"')) {
            end = command.find(L'"', 1);
            parts.File = command.substr(1, end == std::wstring::npos ? end : end - 1);
            end = end == std::wstring::npos ? command.size() : end + 1;
        } else if (exists(command)) {
            parts.File = command;
            end = command.size();
        } else {
            end = command.find(L' ');
            parts.File = command.substr(0, end);
        }

        const size_t paramsAt = end < command.size() ? command.find_first_not_of(L' ', end) : std::wstring::npos;
        if (paramsAt != std::wstring::npos) {
            parts.Params = command.substr(paramsAt);
        }
        return parts;
    }

    std::string Quote(std::string value, const bool alreadyQuoted) {
        if (!alreadyQuoted && value.find_first_of(" &()^%!,;=") == std::string::npos) {
            return value;
        }

        if (value.ends_with('\\')) {
            value += '\\';
        }
        return alreadyQuoted ? value : '"' + value + '"';
    }

    std::string ExpandDir(std::string command, const std::string_view folder) {
        const std::string_view token = DirToken;
        for (size_t at = command.find(token); at != std::string::npos;) {
            const std::string value = Quote(std::string(folder), at > 0 && command[at - 1] == '"');
            command.replace(at, token.size(), value);
            at = command.find(token, at + value.size());
        }
        return command;
    }

    std::string_view WholeCodePoints(const std::string_view text) {
        for (size_t back = 1; back <= 4 && back <= text.size(); ++back) {
            const auto byte = static_cast<unsigned char>(text[text.size() - back]);
            if ((byte & 0xC0) == 0x80) {
                continue;
            }
            const size_t length = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
            return length > back ? text.substr(0, text.size() - back) : text;
        }
        return text;
    }

    std::string Flatten(std::string text, const size_t limit) {
        std::ranges::replace_if(text, [](const char c) { return c == '\r' || c == '\n' || c == '\t'; }, ' ');

        const size_t first = text.find_first_not_of(' ');
        if (first == std::string::npos) {
            return {};
        }
        text = text.substr(first, text.find_last_not_of(' ') - first + 1);

        if (text.size() <= limit) {
            return text;
        }
        return std::string(WholeCodePoints(std::string_view(text).substr(0, limit))) + "...";
    }
}
